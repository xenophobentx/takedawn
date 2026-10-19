"""regen.sh in Python, so no bash is needed (Windows) and every platform runs the
same pipeline: xbe_parser, disasm, func_id, abi_analysis, recomp, from the
toolkit, writing the generated C into src/game/recomp/gen (never committed).

This follows the repo's regen.sh stage for stage and flag for flag; keep the
two in step. Output of both was compared file by file (see docs/installer.md).

The toolkit's modules are started with runpy from a one-line launcher instead of
`python -m`, so an embeddable Python (whose ._pth file ignores PYTHONPATH and
the current directory) works too, with the toolkit and an optional extra site
directory (the capstone wheel unpacked by the installer) put on sys.path.
"""

import os

from common import StepError, run

LAUNCH = ("import os, runpy, sys; "
          "sys.path[:0] = [p for p in (os.getcwd(), os.environ.get('BO3_SITE')) if p]; "
          "mod = sys.argv[1]; sys.argv = [mod] + sys.argv[2:]; "
          "runpy.run_module(mod, run_name='__main__', alter_sys=True)")

STAGES = ("xbe_parser", "disasm", "func_id", "abi_analysis", "recomp")


def inputs(work, region="NTSC-U"):
    """The region's seed list, spin waits and manual file (regen.sh: BO3_REGION)."""
    cfg = os.path.join(work, "config", "pal") if region == "PAL" else os.path.join(work, "config")
    manual = "recomp_manual_pal.c" if region == "PAL" else "recomp_manual.c"
    return (os.path.join(cfg, "seed_functions.json"), os.path.join(cfg, "spin_waits.json"),
            os.path.join(work, "src", "game", "recomp", manual))


def regen(ctx, python, xbe, out, gen_dir, toolkit, work, force_disasm=False, on_stage=None, site=None,
          region="NTSC-U"):
    """Run the pipeline. `ctx` supplies logs/env/nice (see common.run). `region`
    is the disc's (supported.json bo3_region): NTSC-U or PAL."""
    env = dict(ctx.env, PYTHONIOENCODING="utf-8", PYTHONDONTWRITEBYTECODE="1")
    if site:
        env["BO3_SITE"] = site
    os.makedirs(out, exist_ok=True)

    def j(*p):
        return os.path.join(out, *p)

    def stage(name, module, args, log_to=None):
        if on_stage:
            on_stage(name)
        cmd = [python, "-c", LAUNCH, module] + args
        if log_to is None:
            rc = run(ctx, "regen", cmd, cwd=toolkit, env=env)
        else:
            rc = run(ctx, "regen", cmd, cwd=toolkit, env=env, tee=log_to)
        if rc != 0:
            raise StepError("regen-failed", "%s failed with status %d (log: %s)"
                            % (name, rc, log_to or os.path.join(ctx.logs, "regen.log")))

    seeds, spin, manual = inputs(work, region)
    # regen.sh: SPIN_WAITS="" generates without the spin-wait lowering.
    spin_env = os.environ.get("SPIN_WAITS", spin)
    if force_disasm or not os.path.isfile(j("disasm", "functions.json")):
        stage("xbe_parser", "tools.xbe_parser", [xbe, "--json", j("analysis.json"), "--quiet"])
        stage("disasm", "tools.disasm",
              [xbe, "--analysis-json", j("analysis.json")]
              + (["--seed-functions", seeds] if os.path.isfile(seeds) else [])
              + ["-o", j("disasm"), "-v"], log_to=j("disasm.log"))
    stage("func_id", "tools.func_id",
          [xbe, "--functions", j("disasm", "functions.json"), "--strings", j("disasm", "strings.json"),
           "--xrefs", j("disasm", "xrefs.json"), "-o", j("func_id")])
    stage("abi_analysis", "tools.abi_analysis",
          [xbe, "--functions", j("disasm", "functions.json"),
           "--identified", j("func_id", "identified_functions.json"), "--output-dir", j("abi")])
    trace = os.environ.get("TRACE_FUNCS")
    if trace and not os.path.isabs(trace):
        trace = os.path.join(work, trace)
    stage("recomp", "tools.recomp",
          [xbe, "--all", "--split", "1000", "--disasm-dir", j("disasm"), "--func-id-dir", j("func_id"),
           "--abi-dir", j("abi"), "--exclude-manual", manual]
          + (["--spin-waits", spin_env] if spin_env and os.path.isfile(spin_env) else [])
          + (["--trace-functions", trace] if trace else [])
          + ["--gen-dir", gen_dir, "-o", j("recomp")], log_to=j("recomp.log"))
