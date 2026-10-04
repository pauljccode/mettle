"""Reproduce the completion-notification race using Mettle's bundled libraries."""
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=pathlib.Path, help="Mettle checkout to test")
parser.add_argument("expect", choices=("baseline", "candidate"))
args = parser.parse_args()
source = args.source.resolve()
fixture = pathlib.Path(__file__).with_name("eio-notification-probe.c")

with tempfile.TemporaryDirectory(prefix="mettle-eio-test-") as directory:
    build = pathlib.Path(directory)
    for library in ("libev-4.33", "libeio-1.0.2"):
        subprocess.run(["tar", "xzf", str(source / "deps" / (library + ".tar.gz"))],
                       cwd=build, check=True, timeout=30)
        subprocess.run(["./configure", "--disable-shared", "--enable-static"],
                       cwd=build / library, check=True, timeout=120)
        subprocess.run(["make", "-j2"], cwd=build / library, check=True, timeout=120)

    text = (source / "mettle/src/mettle.c").read_text()
    start = text.index("static struct ev_idle eio_idle_watcher;")
    end = text.index("static void\nheartbeat_cb", start)
    (build / "notifier-callbacks.h").write_text(text[start:end])
    binary = build / "eio-notification-probe"
    subprocess.run(["cc", "-pthread", "-I" + str(build),
                    "-I" + str(build / "libev-4.33"),
                    "-I" + str(build / "libeio-1.0.2"), str(fixture),
                    str(build / "libeio-1.0.2/.libs/libeio.a"),
                    str(build / "libev-4.33/.libs/libev.a"), "-lm", "-o", str(binary)],
                   check=True, timeout=60)
    subprocess.run([str(binary), args.expect], check=True, timeout=15)
