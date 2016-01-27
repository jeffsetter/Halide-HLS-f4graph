#include "Halide.h"
#include <string.h>

using namespace Halide;
using std::string;

const unsigned char gaussian2d[5][5] = {
    {1,     3,     6,     3,     1},
    {3,    15,    25,    15,     3},
    {6,    25,    44,    25,     6},
    {3,    15,    25,    15,     3},
    {1,     3,     6,     3,     1}
};

Var x("x"), y("y"), c("c");
Var xo("xo"), xi("xi"), yi("yi"), yo("yo");

class MyPipeline {
public:
    ImageParam input;
    ImageParam weight;
    Param<uint8_t> bias;
    Func clamped;
    Func conv1;
    Func output;
    Func hw_output;
    std::vector<Argument> args;

    MyPipeline() : input(UInt(8), 3, "input"), weight(UInt(8), 2, "weight"),
                   conv1("conv1"), output("output"), hw_output("hw_output") {
        // define the algorithm
        clamped = BoundaryConditions::repeat_edge(input);
        conv1 = convolve55_rd(clamped);
        hw_output = convolve55_rd(conv1);
        output(x, y, c) = hw_output(x, y, c);

        // define common schedule: tile output
        output.tile(x, y, xo, yo, xi, yi, 256, 256);

        // restrict arguments
        weight.set_bounds(0, 0, 5);
        weight.set_bounds(1, 0, 5);
        weight.set_stride(0, 1);
        weight.set_stride(1, 5);

        args.push_back(input);
        args.push_back(weight);
        args.push_back(bias);
    }

    void compile_cpu() {
        std::cout << "\ncompiling cpu code..." << std::endl;

        conv1.store_at(output, xo).compute_at(output, xi);
        clamped.store_at(output, xo).compute_at(output, xi);

        //output.print_loop_nest();
        output.compile_to_lowered_stmt("pipeline_native.ir.html", args, HTML);
        output.compile_to_header("pipeline_native.h", args, "pipeline_native");
        output.compile_to_object("pipeline_native.o", args, "pipeline_native");
    }

    void compile_hls() {
        std::cout << "\ncompiling HLS code..." << std::endl;

        // HLS schedule: make a hw pipeline producing 'hw_output', taking
        // inputs of 'clamped', buffering intermediates at (output, xo) loop
        // level
        hw_output.accelerate({clamped});  // define the inputs and the output
        hw_output.store_at(output, xo);   // define the tile size processed by the pipeline
        hw_output.compute_at(output, xi); // define the throughput of the pipeline

        clamped.linebuffer();  // line-buffer the inputs is required
        conv1.linebuffer();    // line-buffer an intermediate stage is optional

        //output.print_loop_nest();
        output.compile_to_lowered_stmt("pipeline_hls.ir.html", args, HTML);
        output.compile_to_hls("pipeline_hls.cpp", args, "pipeline_hls");
        output.compile_to_header("pipeline_hls.h", args, "pipeline_hls");
    }

private:
    Func convolve55_rd(Func in) {
        Func local_sum, res;
        RDom r(-2, 5, -2, 5);

        local_sum(x, y, c) = cast<uint16_t>(bias);
        local_sum(x, y, c) += cast<uint16_t>(in(x+r.x, y+r.y, c)) * weight(r.x+2, r.y+2);
        res(x, y, c) = cast<uint8_t>(local_sum(x, y, c) >> 8);

        // unroll the reduction
        local_sum.update(0).unroll(r.x).unroll(r.y);
        return res;
    }
};


int main(int argc, char **argv) {
    MyPipeline p1;
    p1.compile_cpu();

    MyPipeline p2;
    p2.compile_hls();

    return 0;
}