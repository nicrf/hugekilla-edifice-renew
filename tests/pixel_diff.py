#!/usr/bin/env python3
"""Compares HK_SHOT captures (binary PPM, P6) pixel by pixel.

  python3 tests/pixel_diff.py before.ppm after.ppm [--tolerance N] [--diff out.ppm]
  python3 tests/pixel_diff.py before_dir/ after_dir/ [--tolerance N] [--ignore before2_dir/ ...]

A pixel differs when one of its channels differs by more than the tolerance
(0 by default). With two folders, every .ppm of the first is compared with the
file of the same name in the second. Exit status 1 if any image differs or is
missing, so it can gate a renderer change (M12.5, M13).

In-game frames are not the same from one run to the next even with HK_SEED
(the camera and animations follow the real clock). --ignore takes other
captures of the *reference* binary: pixels where they differ from `before` are
run-to-run noise and are skipped (the 2D overlays stay compared).
"""
import argparse
import os
import sys


def read_ppm(path):
	with open(path, 'rb') as f:
		data = f.read()
	# header: magic, width, height, maxval, separated by whitespace (comments allowed)
	fields = []
	pos = 0
	while len(fields) < 4:
		while data[pos:pos + 1].isspace():
			pos += 1
		if data[pos:pos + 1] == b'#':
			while data[pos:pos + 1] not in (b'\n', b''):
				pos += 1
			continue
		start = pos
		while not data[pos:pos + 1].isspace():
			pos += 1
		fields.append(data[start:pos])
	pos += 1  # the single whitespace before the pixels
	if fields[0] != b'P6' or int(fields[3]) != 255:
		raise ValueError('%s: only 8-bit P6 PPM is supported' % path)
	w, h = int(fields[1]), int(fields[2])
	px = data[pos:pos + w * h * 3]
	if len(px) != w * h * 3:
		raise ValueError('%s: truncated' % path)
	return w, h, px


def delta(pa, pb, i):
	return max(abs(pa[i] - pb[i]), abs(pa[i + 1] - pb[i + 1]), abs(pa[i + 2] - pb[i + 2]))


def compare(a, b, tolerance, diffpath=None, ignore=()):
	"""Returns (differing pixels, compared pixels, largest channel difference)."""
	wa, ha, pa = read_ppm(a)
	wb, hb, pb = read_ppm(b)
	if (wa, ha) != (wb, hb):
		raise ValueError('size differs: %dx%d vs %dx%d' % (wa, ha, wb, hb))
	noisy = bytearray(wa * ha)
	for path in ignore:
		wn, hn, pn = read_ppm(path)
		if (wn, hn) != (wa, ha):
			raise ValueError('%s: size differs' % path)
		for i in range(0, len(pa), 3):
			if delta(pa, pn, i) > tolerance:
				noisy[i // 3] = 1
	total = wa * ha - sum(noisy)
	if pa == pb:
		return 0, total, 0
	bad = 0
	worst = 0
	out = bytearray(len(pa)) if diffpath else None
	for i in range(0, len(pa), 3):
		if noisy[i // 3]:
			continue
		d = delta(pa, pb, i)
		if d > worst:
			worst = d
		if d > tolerance:
			bad += 1
			if out is not None:
				out[i] = 255  # differing pixels in red on black
	if out is not None:
		with open(diffpath, 'wb') as f:
			f.write(b'P6\n%d %d\n255\n' % (wa, ha))
			f.write(bytes(out))
	return bad, wa * ha, worst


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument('before')
	ap.add_argument('after')
	ap.add_argument('--tolerance', type=int, default=0, help='largest channel difference still counted as equal')
	ap.add_argument('--diff', help='single files: write the differing pixels to this PPM')
	ap.add_argument('--ignore', action='append', default=[], metavar='REF',
		help='another capture (or folder) of the reference: pixels where it differs from before are skipped')
	args = ap.parse_args()

	if os.path.isdir(args.before):
		names = sorted(n for n in os.listdir(args.before) if n.endswith('.ppm'))
		pairs = [(n, os.path.join(args.before, n), os.path.join(args.after, n),
			[os.path.join(d, n) for d in args.ignore if os.path.exists(os.path.join(d, n))]) for n in names]
	else:
		pairs = [(os.path.basename(args.before), args.before, args.after, args.ignore)]

	failed = 0
	for name, a, b, ignore in pairs:
		try:
			bad, total, worst = compare(a, b, args.tolerance, args.diff, ignore)
		except (OSError, ValueError) as e:
			print('FAIL %s: %s' % (name, e))
			failed += 1
			continue
		status = 'same' if bad == 0 else 'DIFF'
		print('%s %s: %d/%d pixels differ (%.3f%%), max channel delta %d' % (status, name, bad, total, 100.0 * bad / max(total, 1), worst))
		failed += bad != 0
	sys.exit(1 if failed else 0)


if __name__ == '__main__':
	main()
