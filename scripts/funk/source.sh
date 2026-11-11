#!/usr/bin/env bash
# Validate the selected project revision; this is a guard, not a signature check.
funk_require_supported_source() {
    local repo=$1 experimental=${2:-} branch head upstream tag published
    git -C "$repo" merge-base --is-ancestor dfbac8d3d9c967003e74b4ffc5610f7d45e8f666 HEAD || {
        echo 'Erwartet wird die Mumble-Funk-Basis Mumble 1.5.939.' >&2; return 1;
    }
    git -C "$repo" diff --quiet HEAD -- || {
        echo 'Versionierte Quelldateien sind verändert. Bitte einen unveränderten freigegebenen Stand verwenden.' >&2; return 1;
    }
    case "$(git -C "$repo" config --get remote.origin.url)" in
        https://github.com/Shawna7han/mumble-funk|https://github.com/Shawna7han/mumble-funk.git|git@github.com:Shawna7han/mumble-funk.git|ssh://git@github.com/Shawna7han/mumble-funk.git) ;;
        *) echo 'origin muss auf das offizielle Shawna7han/mumble-funk-Repository zeigen.' >&2; return 1 ;;
    esac
    branch=$(git -C "$repo" branch --show-current)
    head=$(git -C "$repo" rev-parse HEAD)
    case "$branch" in
        stable|funk-v0.1)
            upstream=$(git -C "$repo" rev-parse --verify "refs/remotes/origin/$branch^{commit}") || return 1
            [[ $head == "$upstream" ]] || {
                echo 'Der lokale Branch weicht von origin ab. Bitte den Quellstand vor dem Setup prüfen.' >&2; return 1;
            }
            ;;
        dev)
            [[ $experimental == --experimental-dev ]] || {
                echo 'EXPERIMENTELL: dev benötigt --experimental-dev.' >&2; return 1;
            }
            upstream=$(git -C "$repo" rev-parse --verify 'refs/remotes/origin/dev^{commit}') || return 1
            [[ $head == "$upstream" ]] || {
                echo 'Lokaler dev-Commit weicht von origin/dev ab. Bitte git fetch und prüfen.' >&2; return 1;
            }
            published=$(git -C "$repo" ls-remote --heads origin refs/heads/dev) || return 1
            [[ $(awk '$2=="refs/heads/dev" {print $1}' <<< "$published") == "$head" ]] || {
                echo 'dev-Commit ist nicht der aktuell veröffentlichte Stand. Bitte git fetch und prüfen.' >&2; return 1;
            }
            echo 'EXPERIMENTELL: dev-Testinstallation, keine Stable-Freigabe; Geräte-Hörtests erforderlich.' >&2
            ;;
        feature/squelch-audio|feature/audio-volume|feature/busy-tone-asset|feature/audio-defaults|feature/radio-noise)
            local required=--experimental-squelch
            if [[ $branch == feature/audio-volume ]]; then required=--experimental-audio-volume; fi
            if [[ $branch == feature/busy-tone-asset ]]; then required=--experimental-busy-tone; fi
            if [[ $branch == feature/audio-defaults ]]; then required=--experimental-audio-defaults; fi
            if [[ $branch == feature/radio-noise ]]; then
                required=--experimental-radio-noise
                # Ignored build outputs remain allowed; unpublished source files do not.
                local untracked
                untracked=$(git -C "$repo" ls-files --others --exclude-standard) || return 1
                [[ -z $untracked ]] || {
                    echo 'Nicht versionierte Dateien vorhanden. Bitte einen sauberen veröffentlichten Quellstand verwenden.' >&2; return 1;
                }
            fi
            [[ $experimental == "$required" ]] || {
                echo "EXPERIMENTELL: $branch benötigt $required." >&2; return 1;
            }
            upstream=$(git -C "$repo" rev-parse --verify "refs/remotes/origin/$branch^{commit}") || return 1
            [[ $head == "$upstream" ]] || { echo 'Experimenteller Branch weicht von origin ab.' >&2; return 1; }
            published=$(git -C "$repo" ls-remote --heads origin "refs/heads/$branch") || return 1
            [[ $(awk -v ref="refs/heads/$branch" '$2==ref {print $1}' <<< "$published") == "$head" ]] || {
                echo 'Experimenteller Commit ist nicht der veröffentlichte Feature-Stand. Bitte git fetch und prüfen.' >&2; return 1;
            }
            echo 'EXPERIMENTELL: Audio-Vorschau, keine Stable-Freigabe; Geräte-Hörtest des gewählten Stands erforderlich.' >&2
            ;;
        '')
            # Detached checkouts must be an annotated, remotely published project
            # version. Do not accept a locally invented tag or an archive tag.
            published=false
            while IFS= read -r tag; do
                [[ $tag =~ ^funk-v[0-9]+\.[0-9]+\.[0-9]+$ ]] || continue
                [[ $(git -C "$repo" cat-file -t "refs/tags/$tag") == tag ]] || continue
                upstream=$(git -C "$repo" ls-remote --tags origin "refs/tags/$tag" "refs/tags/$tag^{}") || return 1
                if [[ $(awk -v ref="refs/tags/$tag^{}" '$2==ref {print $1}' <<< "$upstream") == "$head" ]]; then
                    published=true; break
                fi
            done < <(git -C "$repo" tag --points-at HEAD)
            [[ $published == true ]] || {
                echo 'Detached HEAD benötigt einen veröffentlichten annotierten funk-vX.Y.Z-Versionstag.' >&2; return 1;
            }
            ;;
        *) echo 'Installation nur aus stable, funk-v0.1 oder einem veröffentlichten Versionstag.' >&2; return 1 ;;
    esac
    printf 'Mumble-Funk-Quellrevision: %s\n' "$head"
}
