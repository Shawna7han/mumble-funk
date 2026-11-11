"""Source-selection safety checks using real Git repositories, without GitHub writes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
OFFICIAL = "https://github.com/Shawna7han/mumble-funk.git"


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "checkout"
        self.remote = self.root / "published.git"
        self.run_git("clone", "--shared", "--no-checkout", str(REPO), str(self.repo), cwd=self.root)
        self.git("config", "user.name", "Source guard test")
        self.git("config", "user.email", "test@example.invalid")
        self.git("checkout", "-B", "stable", "HEAD")
        self.head = self.git("rev-parse", "HEAD").stdout.strip()
        self.git("update-ref", "refs/remotes/origin/stable", self.head)
        self.git("update-ref", "refs/remotes/origin/funk-v0.1", self.head)
        self.run_git("init", "--bare", str(self.remote), cwd=self.root)
        self.git("remote", "set-url", "origin", OFFICIAL)
        # Only this disposable repository redirects the public URL to its local
        # publication fixture. Production validation still checks the raw URL.
        self.git("config", f"url.{self.remote}.insteadOf", OFFICIAL)

    def run_git(self, *args, cwd=None):
        # A detached CI checkout can trigger auto-maintenance after our local
        # fixture commits. Keep it synchronous/off so no Git child recreates
        # files while TemporaryDirectory removes its disposable repository.
        return subprocess.run(["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false",
                               "-c", "gc.autoDetach=false", *args], cwd=cwd or self.repo, check=True,
                              capture_output=True, text=True)

    def git(self, *args):
        return self.run_git(*args)

    def guard(self, allowed, experimental=False, repo=None):
        result = subprocess.run(["bash", "-eu", "-c",
                                 'source "$1"; funk_require_supported_source "$2" "$3"', "source-test",
                                 str(REPO / "scripts/funk/source.sh"), str(repo or self.repo), ("--experimental-squelch" if experimental is True else experimental) if experimental else ""],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, allowed, result.stdout + result.stderr)
        if allowed:
            self.assertIn(self.head, result.stdout)

    def test_stable_and_legacy_branch(self):
        self.guard(True)
        self.git("checkout", "-B", "funk-v0.1", self.head)
        self.guard(True)

    def test_unknown_branch_rejected(self):
        for branch in ("dev", "feature/audio-engine", "feature/audio-feedback"):
            with self.subTest(branch=branch):
                self.git("checkout", "-B", branch, self.head)
                self.guard(False)

    def dev(self):
        self.git("checkout", "-B", "dev", self.head)
        self.git("update-ref", "refs/remotes/origin/dev", self.head)
        self.git("push", str(self.remote), "HEAD:refs/heads/dev")

    def test_dev_requires_own_flag_and_preserves_other_branch_guards(self):
        self.dev()
        self.guard(False)
        self.guard(False, experimental="--experimental-audio-defaults")
        self.guard(True, experimental="--experimental-dev")
        for branch in ("feature/audio-defaults", "feature/experimental-dev-install", "feature/audio-feedback"):
            self.git("checkout", "-B", branch, self.head)
            self.guard(False, experimental="--experimental-dev")

    def test_dev_requires_exact_cached_and_live_commit(self):
        self.dev()
        base = "dfbac8d3d9c967003e74b4ffc5610f7d45e8f666"
        self.git("update-ref", "-d", "refs/remotes/origin/dev")
        self.guard(False, experimental="--experimental-dev")
        self.git("update-ref", "refs/remotes/origin/dev", base)
        self.guard(False, experimental="--experimental-dev")
        self.git("update-ref", "refs/remotes/origin/dev", self.head)
        self.run_git("--git-dir", str(self.remote), "update-ref", "refs/heads/dev", base)
        self.guard(False, experimental="--experimental-dev")
        self.run_git("--git-dir", str(self.remote), "update-ref", "-d", "refs/heads/dev")
        self.guard(False, experimental="--experimental-dev")
        self.git("push", str(self.remote), "HEAD:refs/heads/dev")
        self.git("commit", "--allow-empty", "-m", "Local dev change")
        # Even a manually advanced cached ref cannot authorize unpublished work.
        self.git("update-ref", "refs/remotes/origin/dev", "HEAD")
        self.guard(False, experimental="--experimental-dev")
        # Nor can stale local/cached refs authorize an older published revision.
        self.git("push", str(self.remote), "HEAD:refs/heads/dev")
        self.git("checkout", "-B", "dev", self.head)
        self.git("update-ref", "refs/remotes/origin/dev", self.head)
        self.guard(False, experimental="--experimental-dev")

    def test_dev_rejects_dirty_source_foreign_origin_and_network_failure(self):
        self.dev()
        readme = self.repo / "README.md"
        original = readme.read_bytes()
        readme.write_bytes(original + b"\nUncommitted change\n")
        self.guard(False, experimental="--experimental-dev")
        readme.write_bytes(original)
        self.git("remote", "set-url", "origin", str(self.remote))
        self.guard(False, experimental="--experimental-dev")
        self.git("remote", "set-url", "origin", OFFICIAL)
        self.git("config", "--unset", f"url.{self.remote}.insteadOf")
        self.git("config", f"url.{self.root / 'missing.git'}.insteadOf", OFFICIAL)
        self.guard(False, experimental="--experimental-dev")

    def test_dev_rejects_unrelated_mumble_base(self):
        self.git("checkout", "--orphan", "unrelated")
        self.git("commit", "--allow-empty", "-m", "Unrelated source")
        self.git("branch", "-M", "dev")
        self.git("update-ref", "refs/remotes/origin/dev", "HEAD")
        self.git("push", str(self.remote), "HEAD:refs/heads/dev")
        self.guard(False, experimental="--experimental-dev")

    def feature(self):
        self.git("checkout", "-B", "feature/squelch-audio", self.head)
        self.git("update-ref", "refs/remotes/origin/feature/squelch-audio", self.head)
        self.git("push", str(self.remote), "HEAD:refs/heads/feature/squelch-audio")

    def radio_noise(self):
        branch = "feature/radio-noise"
        self.git("checkout", "-B", branch, self.head)
        self.git("update-ref", f"refs/remotes/origin/{branch}", self.head)
        self.git("push", str(self.remote), f"HEAD:refs/heads/{branch}")

    def test_radio_noise_requires_own_flag(self):
        self.radio_noise()
        for option in (False, "--experimental-dev", "--experimental-squelch",
                       "--experimental-audio-volume", "--experimental-busy-tone",
                       "--experimental-audio-defaults"):
            self.guard(False, experimental=option)
        self.guard(True, experimental="--experimental-radio-noise")
        self.git("checkout", "-B", "feature/unknown-audio", self.head)
        self.guard(False, experimental="--experimental-radio-noise")

    def test_radio_noise_documented_switch_from_older_single_branch_clone(self):
        self.radio_noise()
        checkout = self.root / "older-single-branch"
        self.run_git("clone", "--shared", "--single-branch", "--branch", "stable",
                     str(self.repo), str(checkout), cwd=self.root)
        self.run_git("-C", str(checkout), "remote", "set-url", "origin", OFFICIAL)
        self.run_git("-C", str(checkout), "config", f"url.{self.remote}.insteadOf", OFFICIAL)
        self.run_git("-C", str(checkout), "fetch", "origin",
                     "refs/heads/feature/radio-noise:refs/remotes/origin/feature/radio-noise")
        self.run_git("-C", str(checkout), "switch", "--no-track", "-c", "feature/radio-noise",
                     "refs/remotes/origin/feature/radio-noise")
        self.run_git("-C", str(checkout), "merge", "--ff-only", "origin/feature/radio-noise")
        self.guard(True, experimental="--experimental-radio-noise", repo=checkout)

    def test_radio_noise_requires_exact_cached_and_live_commit(self):
        self.radio_noise()
        option = "--experimental-radio-noise"
        ref = "refs/remotes/origin/feature/radio-noise"
        live = "refs/heads/feature/radio-noise"
        base = "dfbac8d3d9c967003e74b4ffc5610f7d45e8f666"
        self.git("update-ref", "-d", ref)
        self.guard(False, experimental=option)
        self.git("update-ref", ref, base)
        self.guard(False, experimental=option)
        self.git("update-ref", ref, self.head)
        self.run_git("--git-dir", str(self.remote), "update-ref", live, base)
        self.guard(False, experimental=option)
        self.run_git("--git-dir", str(self.remote), "update-ref", "-d", live)
        self.guard(False, experimental=option)
        self.git("push", str(self.remote), f"HEAD:{live}")
        self.git("commit", "--allow-empty", "-m", "Unpublished radio source")
        self.guard(False, experimental=option)
        self.git("update-ref", ref, "HEAD")
        self.guard(False, experimental=option)
        self.git("push", str(self.remote), f"HEAD:{live}")
        self.git("checkout", "-B", "feature/radio-noise", self.head)
        self.git("update-ref", ref, self.head)
        self.guard(False, experimental=option)

    def test_radio_noise_rejects_dirty_source_foreign_origin_and_network_failure(self):
        self.radio_noise()
        option = "--experimental-radio-noise"
        readme = self.repo / "README.md"
        original = readme.read_bytes()
        readme.write_bytes(original + b"\nUncommitted change\n")
        self.guard(False, experimental=option)
        self.git("add", "README.md")
        self.guard(False, experimental=option)
        readme.write_bytes(original)
        self.git("add", "README.md")
        untracked = self.repo / "unpublished-source.cpp"
        untracked.write_text("// Not published\n")
        self.guard(False, experimental=option)
        untracked.unlink()
        (self.repo / "build").mkdir()
        (self.repo / "build" / "ignored-output").touch()
        self.guard(True, experimental=option)
        self.git("remote", "set-url", "origin", str(self.remote))
        self.guard(False, experimental=option)
        self.git("remote", "set-url", "origin", OFFICIAL)
        self.git("config", "--unset", f"url.{self.remote}.insteadOf")
        self.git("config", f"url.{self.root / 'missing.git'}.insteadOf", OFFICIAL)
        self.guard(False, experimental=option)

    def test_radio_noise_rejects_unrelated_mumble_base(self):
        self.git("checkout", "--orphan", "unrelated")
        self.git("commit", "--allow-empty", "-m", "Unrelated source")
        self.git("branch", "-M", "feature/radio-noise")
        self.git("update-ref", "refs/remotes/origin/feature/radio-noise", "HEAD")
        self.git("push", str(self.remote), "HEAD:refs/heads/feature/radio-noise")
        self.guard(False, experimental="--experimental-radio-noise")

    def test_experimental_branch_requires_explicit_opt_in(self):
        self.feature()
        self.guard(False)
        self.guard(True, experimental=True)

    def test_experimental_publication_must_match_cached_and_live_revision(self):
        self.feature()
        self.git("update-ref", "refs/remotes/origin/feature/squelch-audio", "dfbac8d3d9c967003e74b4ffc5610f7d45e8f666")
        self.guard(False, experimental=True)
        self.git("update-ref", "refs/remotes/origin/feature/squelch-audio", self.head)
        self.run_git("--git-dir", str(self.remote), "update-ref", "refs/heads/feature/squelch-audio", "dfbac8d3d9c967003e74b4ffc5610f7d45e8f666")
        self.guard(False, experimental=True)

    def test_experimental_flag_does_not_whitelist_other_branches_or_dirty_source(self):
        self.git("checkout", "-B", "dev", self.head)
        self.guard(False, experimental=True)
        self.feature()
        (self.repo/"README.md").write_text("uncommitted change")
        self.guard(False, experimental=True)

    def test_audio_volume_requires_its_own_flag_and_published_clean_head(self):
        self.git("checkout", "-B", "feature/audio-volume", self.head)
        self.git("update-ref", "refs/remotes/origin/feature/audio-volume", self.head)
        self.git("push", str(self.remote), "HEAD:refs/heads/feature/audio-volume")
        self.guard(False)
        self.guard(False, experimental=True)  # Old Squelch opt-in is not interchangeable.
        self.guard(True, experimental="--experimental-audio-volume")
        self.git("update-ref", "refs/remotes/origin/feature/audio-volume", "dfbac8d3d9c967003e74b4ffc5610f7d45e8f666")
        self.guard(False, experimental="--experimental-audio-volume")
        self.git("update-ref", "refs/remotes/origin/feature/audio-volume", self.head)
        self.run_git("--git-dir", str(self.remote), "update-ref", "refs/heads/feature/audio-volume", "dfbac8d3d9c967003e74b4ffc5610f7d45e8f666")
        self.guard(False, experimental="--experimental-audio-volume")
        self.git("push", str(self.remote), "HEAD:refs/heads/feature/audio-volume")
        (self.repo/"README.md").write_text("uncommitted source")
        self.guard(False, experimental="--experimental-audio-volume")

    def test_busy_asset_requires_own_flag_clean_and_published_head(self):
        self.git('checkout','-B','feature/busy-tone-asset',self.head)
        self.git('update-ref','refs/remotes/origin/feature/busy-tone-asset',self.head)
        self.git('push',str(self.remote),'HEAD:refs/heads/feature/busy-tone-asset')
        self.guard(False)
        self.guard(False, experimental='--experimental-audio-volume')
        self.guard(True, experimental='--experimental-busy-tone')
        self.git('update-ref','refs/remotes/origin/feature/busy-tone-asset','dfbac8d3d9c967003e74b4ffc5610f7d45e8f666')
        self.guard(False, experimental='--experimental-busy-tone')
        self.git('update-ref','refs/remotes/origin/feature/busy-tone-asset',self.head)
        self.run_git('--git-dir',str(self.remote),'update-ref','refs/heads/feature/busy-tone-asset','dfbac8d3d9c967003e74b4ffc5610f7d45e8f666')
        self.guard(False, experimental='--experimental-busy-tone')
        self.git('push',str(self.remote),'HEAD:refs/heads/feature/busy-tone-asset')
        (self.repo/'README.md').write_text('uncommitted source')
        self.guard(False, experimental='--experimental-busy-tone')

    def test_audio_defaults_requires_own_flag_clean_and_published_head(self):
        self.git('checkout','-B','feature/audio-defaults',self.head)
        self.git('update-ref','refs/remotes/origin/feature/audio-defaults',self.head)
        self.git('push',str(self.remote),'HEAD:refs/heads/feature/audio-defaults')
        self.guard(False)
        self.guard(False, experimental='--experimental-busy-tone')
        self.guard(True, experimental='--experimental-audio-defaults')
        self.git('update-ref','refs/remotes/origin/feature/audio-defaults','dfbac8d3d9c967003e74b4ffc5610f7d45e8f666')
        self.guard(False, experimental='--experimental-audio-defaults')
        self.git('update-ref','refs/remotes/origin/feature/audio-defaults',self.head)
        self.run_git('--git-dir',str(self.remote),'update-ref','refs/heads/feature/audio-defaults','dfbac8d3d9c967003e74b4ffc5610f7d45e8f666')
        self.guard(False, experimental='--experimental-audio-defaults')
        self.git('push',str(self.remote),'HEAD:refs/heads/feature/audio-defaults')
        (self.repo/'README.md').write_text('uncommitted source')
        self.guard(False, experimental='--experimental-audio-defaults')

    def test_local_branch_divergence_rejected(self):
        self.git("commit", "--allow-empty", "-m", "Unpublished local change")
        self.guard(False)

    def test_missing_remote_branch_rejected(self):
        self.git("update-ref", "-d", "refs/remotes/origin/stable")
        self.guard(False)

    def test_tracked_edits_rejected(self):
        with (self.repo / "README.md").open("a") as target:
            target.write("\nLocal change\n")
        self.guard(False)

    def test_foreign_origin_rejected(self):
        self.git("remote", "set-url", "origin", str(self.remote))
        self.guard(False)

    def test_wrong_mumble_base_rejected(self):
        self.git("checkout", "--orphan", "unrelated")
        self.git("commit", "--allow-empty", "-m", "Unrelated source")
        self.git("branch", "-M", "stable")
        self.git("update-ref", "refs/remotes/origin/stable", "HEAD")
        self.guard(False)

    def tag(self, name="funk-v99.0.0", annotated=True, publish=True):
        if annotated:
            self.git("tag", "-a", name, "-m", "Disposable local test tag")
        else:
            self.git("tag", name)
        if publish:
            self.git("push", str(self.remote), f"refs/tags/{name}")
        self.git("checkout", "--detach", self.head)

    def test_published_annotated_version(self):
        self.tag()
        self.guard(True)

    def test_unpublished_version_rejected(self):
        self.tag(publish=False)
        self.guard(False)

    def test_lightweight_version_rejected(self):
        self.tag(annotated=False)
        self.guard(False)

    def test_archive_tag_rejected(self):
        self.tag(name="archive/mumble/old")
        self.guard(False)

    def test_remote_tag_commit_mismatch_rejected(self):
        self.tag()
        self.run_git("--git-dir", str(self.remote), "update-ref", "refs/tags/funk-v99.0.0",
                     "dfbac8d3d9c967003e74b4ffc5610f7d45e8f666")
        self.guard(False)

    def test_remote_unavailable_fails_closed(self):
        self.tag()
        self.git("config", f"url.{self.root / 'missing.git'}.insteadOf", OFFICIAL)
        self.git("config", "--unset", f"url.{self.remote}.insteadOf")
        self.guard(False)

    def test_worktree_lock_path_is_absolute_from_another_directory(self):
        worktree = self.root / "linked-checkout"
        self.git("worktree", "add", "--detach", str(worktree), self.head)
        result = self.run_git("-C", str(worktree), "rev-parse", "--path-format=absolute",
                              "--git-path", "funk-setup.lock", cwd=self.root)
        path = Path(result.stdout.strip())
        self.assertTrue(path.is_absolute())
        self.assertTrue(path.parent.is_dir())
        path.touch()


if __name__ == "__main__":
    unittest.main(verbosity=2)
