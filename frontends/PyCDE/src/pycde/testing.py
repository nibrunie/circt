from pycde import System, module

import builtins
import inspect
from pathlib import Path
import subprocess
import inspect


def unittestmodule(generate=True,
                   print=True,
                   run_passes=False,
                   emit_outputs=False,
                   **kwargs):
    """
    Like @module, but additionally performs system instantiation, generation,
    and printing to reduce boilerplate in tests.
    In case of wrapping a function, @testmodule accepts kwargs which are passed
    to the function as arguments.
    """

    def testmodule_inner(func_or_class):
        mod = module(func_or_class)

        # Apply any provided kwargs if this was a function.
        if inspect.isfunction(func_or_class):
            mod = mod(**kwargs)

        # Add the module to global scope in case it's referenced within the
        # module generator functions
        setattr(builtins, mod.__name__, mod)

        sys = System([mod])
        if generate:
            sys.generate()
            if print:
                sys.print()
            if run_passes:
                sys.run_passes()
            if emit_outputs:
                sys.emit_outputs()

        return mod

    return testmodule_inner


def pycdetest(func):
    # Set a flag on the function to indicate that it's a testbench.
    setattr(func, "_testbench", True)
    return func


def gen_cocotb_makefile(top, sim):
    template = f"""
TOPLEVEL_LANG = verilog
VERILOG_SOURCES = $(shell pwd)/{top}.sv
TOPLEVEL = {top}
MODULE = test_{top}
SIM={sim}

include $(shell cocotb-config --makefiles)/Makefile.sim
"""
    return template


def gen_cocotb_testfile(tests):
    template = "import cocotb\n\n"

    for test in tests:
        src = inspect.getsourcelines(test)[0]
        # This seems a little janky, but we need to clean up the test function...
        # Find indent of the first line.
        indent = len(src[0]) - len(src[0].lstrip())
        # remove the indent from the lines.
        src = [line[indent:] for line in src]
        # Remove the decorator
        src = src[1:]
        # If the function was not async, make it so
        if not src[0].startswith("async"):
            src[0] = "async " + src[0]

        # Append to the template as a cocotb test
        template += "@cocotb.test()\n"
        template += "".join(src)
        template += "\n\n"

    return template


def fix_icarus_sv_file(fn):
    """HACK HACK HACK
    Replace all always_# in the output file with 'always'
    This is a hack to get the testbench to work with iverilog.

    Erase PyCDE parameters... iverilog doesn't like those either.
    """

    patterns = {
        "always_comb": "always",
        "always_latch": "always",
        "always_ff": "always",
        # need a default value for parameters, else it's considered a syntax error by iverilog
        "parameter __INST_HIER": "parameter __INST_HIER=0"

    }

    with open(fn, "r") as f:
        lines = f.readlines()
    with open(fn, "w") as f:
        for line in lines:
            for pattern, replacement in patterns.items():
                line = line.replace(pattern, replacement)
            f.write(line)


def testbench(pycde_mod, simulator="icarus"):
    def testbenchmodule_inner(tb_class):
        sys = System([pycde_mod])
        sys.generate()
        sys.emit_outputs()

        # Generate cocotb makefile
        makefile_path = Path(sys._output_directory, "Makefile")
        with open(makefile_path, "w") as f:
            f.write(gen_cocotb_makefile(pycde_mod.__name__, simulator))

        # Find functions with the testbench flag set.
        testbench_funcs = [
            getattr(tb_class, a) for a in dir(tb_class)
            if getattr(getattr(tb_class, a), "_testbench", False)
        ]

        out_sv = Path(sys._output_directory, f"{pycde_mod.__name__}.sv")
        # ... fix things to make iverilog happy
        fix_icarus_sv_file(out_sv)

        # Generatae the cocotb test file
        testfile_path = Path(sys._output_directory,
                             f"test_{pycde_mod.__name__}.py")
        with open(testfile_path, "w") as f:
            f.write(gen_cocotb_testfile(testbench_funcs))

        # Run 'make' in the output directory and let cocotb do its thing.
        subprocess.run(["make"], cwd=sys._output_directory)

        return pycde_mod
    return testbenchmodule_inner
