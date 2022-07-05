from pycde import Input, Output, generator, module, Clock
from pycde.pycde_types import types
from pycde.testing import testbench, pycdetest
from pycde.dialects import comb


@module
class RegAdd:
    rst = Input(types.i1)
    clk = Clock()
    in1 = Input(types.i16)
    in2 = Input(types.i16)
    out = Output(types.i16)

    @generator
    def build(ports):
        addRes = comb.AddOp(ports.in1, ports.in2)
        ports.out = addRes.reg(clk=ports.clk, rst=ports.rst)


@testbench(RegAdd)
class RegAddTester:

    @pycdetest
    async def random_test(ports):
        import cocotb
        import cocotb.clock
        from cocotb.triggers import FallingEdge
        import random

        # Create a 10us period clock on port clk
        clock = cocotb.clock.Clock(ports.clk, 10, units="us")
        cocotb.start_soon(clock.start())  # Start the clock

        for i in range(10):
            in1 = random.randint(0, 100)
            in2 = random.randint(0, 100)
            ports.in1.value = in1
            ports.in2.value = in2
            await FallingEdge(ports.clk)
            assert ports.out.value == (in1 + in2), "output q was incorrect on the {}th cycle".format(
                i)

    @pycdetest
    async def inc_test(ports):
        import cocotb
        import cocotb.clock
        from cocotb.triggers import FallingEdge

        # Create a 10us period clock on port clk
        clock = cocotb.clock.Clock(ports.clk, 10, units="us")
        cocotb.start_soon(clock.start())  # Start the clock

        # Manual reset, the FF resert isn't getting emitted in the sv...
        ports.in1.value = 0
        ports.in2.value = 0
        await FallingEdge(ports.clk)

        acc = 0
        for i in range(10):
            acc += 1
            ports.in1.value = ports.out.value
            ports.in2.value = 1
            await FallingEdge(ports.clk)
            assert ports.out.value == (acc), "output q was incorrect on the {}th cycle".format(
                i)
