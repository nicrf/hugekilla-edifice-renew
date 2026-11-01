// Edifice web build (roadmap M14), run before main(): the user folder (net.ini,
// control.ini, sv.ini, maps/) lives in IndexedDB, loaded here before the game reads
// it; platform_sdl.cpp saves it back when the game writes there.
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
	FS.mkdir('/user');
	FS.mount(IDBFS, {}, '/user');
	addRunDependency('edifice-user');
	FS.syncfs(true, function (err) {
		if (err) console.warn('Edifice: loading the settings failed', err);
		removeRunDependency('edifice-user');
	});

	// ?HK_MENU=join&HK_SEED=42...: the debug aids the native game reads from its
	// environment (BUILD.md), for tests in a browser
	new URLSearchParams(location.search).forEach(function (value, key) {
		if (/^HK_[A-Z0-9_]+$/.test(key)) ENV[key] = value;
	});

	// leaving the page may come before the next save (at most once a second)
	function save() {
		FS.syncfs(false, function (err) {
			if (err) console.warn('Edifice: saving the settings failed', err);
		});
	}
	addEventListener('pagehide', save);
	document.addEventListener('visibilitychange', function () {
		if (document.visibilityState === 'hidden') save();
	});
});
