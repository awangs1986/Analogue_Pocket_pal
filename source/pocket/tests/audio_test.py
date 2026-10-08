#!/usr/bin/env python3
"""Synthetic, redistributable-fixture host tests. Requires C compiler + ffmpeg.

No game assets are used. Builds existing vendored decoder code, then checks
bounded cooperative work, PCM accuracy, channel mapping, rates, loops, malformed
inputs, backpressure, and ring wrap. Generated files live in a temp directory.
"""
import array
import math
import pathlib
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest

POCKET = pathlib.Path(__file__).resolve().parents[1]
VORBIS = POCKET.parent / "sdlpal" / "liboggvorbis"


class AudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("cc") or not shutil.which("ffmpeg"):
            raise unittest.SkipTest("cc and ffmpeg are needed for synthetic audio tests")
        cls.temp = tempfile.TemporaryDirectory(prefix="pal-pocket-audio-")
        cls.work = pathlib.Path(cls.temp.name)
        cls.binary = cls.work / "audio-test"
        cls.reference_binary = cls.work / "audio-reference"
        subprocess.run(["cc", "-std=gnu99", "-O2", "-g", "-I" + str(POCKET),
                        "-I" + str(VORBIS / "include"), "-I" + str(VORBIS / "src"),
                        str(POCKET / "tests/audio_test.c"), str(POCKET / "audio_stream.c"),
                        *map(str, sorted((VORBIS / "src").glob("*.c"))),
                        *shlex.split(os.environ.get("PPA_TEST_CFLAGS", "")),
                        "-lm", "-o", str(cls.binary)], check=True)
        subprocess.run(["cc", "-std=gnu99", "-I" + str(VORBIS / "include"),
                        str(POCKET / "tests/audio_reference.c"),
                        "-l:libvorbisfile.so.3", "-l:libvorbis.so.0", "-l:libogg.so.0",
                        "-o", str(cls.reference_binary)], check=True)
        sdk = pathlib.Path(os.environ.get("SDK_ROOT", str(POCKET.parent.parent / "openfpgaSDK")))
        cls.adapter_binary = None
        if (sdk / "src/sdk/include/SDL.h").exists():
            cls.adapter_binary = cls.work / "audio-adapter-test"
            subprocess.run(["cc", "-std=gnu99", "-O2", "-D__POCKET__=1", "-DUSE_SDL3=0",
                            "-I" + str(POCKET), "-I" + str(POCKET.parent / "sdlpal"),
                            "-I" + str(POCKET.parent / "sdlpal/sdl_compat"),
                            "-I" + str(VORBIS / "include"), "-I" + str(VORBIS / "src"),
                            "-I" + str(sdk / "src/sdk/include"),
                            "-include", str(POCKET / "tests/audio_pc_shim.h"),
                            str(POCKET / "tests/audio_adapter_test.c"),
                            str(POCKET / "audio.c"), str(POCKET / "audio_stream.c"),
                            *map(str, sorted((VORBIS / "src").glob("*.c"))),
                            *shlex.split(os.environ.get("PPA_TEST_CFLAGS", "")),
                            "-lm", "-o", str(cls.adapter_binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "temp"):
            cls.temp.cleanup()

    def fixture(self, rate=48000, channels=2):
        path = self.work / f"tone-{rate}-{channels}.ogg"
        expression = "|".join(f"0.2*sin(2*PI*{220 + 110 * ch}*t)"
                              for ch in range(channels))
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-f", "lavfi",
                        "-i", f"aevalsrc={expression}:s={rate}:d=0.35", "-c:a", "libvorbis",
                        "-q:a", "4", str(path)], check=True)
        return path

    def run_decoder(self, path, loop=False, ok=True):
        output = self.work / "output.pcm"
        result = subprocess.run([str(self.binary), str(path), str(output), str(int(loop))],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0 if ok else 1, result.stdout + result.stderr)
        pcm = array.array("h")
        pcm.frombytes(output.read_bytes())
        return pcm, result.stdout

    def reference(self, path):
        result = subprocess.run([str(self.reference_binary), str(path)],
                                capture_output=True, check=True)
        pcm = array.array("f")
        pcm.frombytes(result.stdout)
        return pcm

    def check_rate(self, rate, channels):
        path = self.fixture(rate, channels)
        actual, report = self.run_decoder(path)
        source = self.reference(path)
        frames = len(source) // channels
        expected_frames = math.ceil(frames * 48000 / rate)
        self.assertIn("state=3", report)
        self.assertEqual(len(actual) // 2, expected_frames, report)
        max_error = 0
        for frame in range(expected_frames):
            position, phase = divmod(frame * rate, 48000)
            next_pos = min(position + 1, frames - 1)
            for channel in range(2):
                source_channel = channel if channels == 2 else 0
                a = source[position * channels + source_channel]
                b = source[next_pos * channels + source_channel]
                expected = max(-32768, min(32767, int((a + (b - a) * phase / 48000) * 32768)))
                max_error = max(max_error, abs(actual[frame * 2 + channel] - expected))
        self.assertLessEqual(max_error, 3, report)
        if channels == 1:
            self.assertEqual(actual[0::2], actual[1::2])
        return path, actual

    def test_stereo_48000(self):
        self.check_rate(48000, 2)

    def test_adapter_partial_writes_and_cd_failure(self):
        if self.adapter_binary is None:
            self.skipTest("openfpgaSDK not present; set SDK_ROOT for adapter tests")
        result = subprocess.run([str(self.adapter_binary)], capture_output=True,
                                text=True, timeout=30, cwd=self.work)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS", result.stdout)

    def test_mono_44100_linear_upsampling(self):
        self.check_rate(44100, 1)

    def test_stereo_22050_linear_upsampling(self):
        self.check_rate(22050, 2)

    def test_mono_8000_linear_upsampling(self):
        self.check_rate(8000, 1)

    def test_loop_repeats_exact_pcm(self):
        path, once = self.check_rate(48000, 2)
        repeated, report = self.run_decoder(path, loop=True)
        self.assertIn("loops=2", report)
        for index, sample in enumerate(repeated):
            self.assertEqual(sample, once[index % len(once)], f"sample {index}: {report}")

    def test_reject_high_rate(self):
        _, report = self.run_decoder(self.fixture(96000, 2), ok=False)
        self.assertIn("8000 through 48000", report)

    def test_reject_surround(self):
        _, report = self.run_decoder(self.fixture(48000, 6), ok=False)
        self.assertIn("Only mono or stereo", report)

    def test_truncated_and_invalid(self):
        original = self.fixture().read_bytes()
        path = self.work / "broken.ogg"
        for data in (b"", b"not an Ogg file" * 1000, original[:200], original[:-100]):
            path.write_bytes(data)
            _, report = self.run_decoder(path, ok=False)
            self.assertIn("state=4", report)

    def test_corrupt_page_checksum(self):
        damaged = bytearray(self.fixture().read_bytes())
        damaged[-50] ^= 0x7f
        path = self.work / "corrupt.ogg"
        path.write_bytes(damaged)
        _, report = self.run_decoder(path, ok=False)
        self.assertIn("checksum", report)


if __name__ == "__main__":
    unittest.main(verbosity=2)
