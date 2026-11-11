#pragma once
#include "Harness.h"
namespace {
struct CodecGate {
	QTemporaryDir directory;
	int entered = -1, release = -1;
	pid_t workerThread = 0;
	bool attach(Fixture &fixture) {
		const auto ready  = directory.filePath("entered").toUtf8();
		const auto permit = directory.filePath("release").toUtf8();
		if (::mkfifo(ready.constData(), 0600) || ::mkfifo(permit.constData(), 0600))
			return false;
		entered  = ::open(ready.constData(), O_RDWR | O_NONBLOCK);
		release  = ::open(permit.constData(), O_RDWR | O_NONBLOCK);
		auto env = QProcessEnvironment::systemEnvironment();
		env.insert("MUMBLE_TEST_VOICE_GATE", directory.path());
		fixture.server.setProcessEnvironment(env);
		return entered >= 0 && release >= 0;
	}
	bool wait() {
		pollfd fd{ entered, POLLIN, 0 };
		return ::poll(&fd, 1, 3000) == 1 && (fd.revents & POLLIN)
			   && ::read(entered, &workerThread, sizeof(workerThread)) == sizeof(workerThread);
	}
	void resume() {
		if (release >= 0) {
			const char signal = 'x';
			const auto n      = ::write(release, &signal, 1);
			(void) n;
		}
	}
	~CodecGate() {
		resume();
		if (entered >= 0)
			::close(entered);
		if (release >= 0)
			::close(release);
	}
};
} // namespace
