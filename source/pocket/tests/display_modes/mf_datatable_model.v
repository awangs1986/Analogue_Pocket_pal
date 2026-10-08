// SPDX-License-Identifier: Apache-2.0
// Simulation-only replacement of the vendor BRAM. Not FPGA synthesis input.
// Display-mode commands do not access it; model normal synchronous behavior.
`timescale 1ns/1ps
module mf_datatable (
    input wire [9:0] address_a, address_b,
    input wire clock_a, clock_b,
    input wire [31:0] data_a, data_b,
    input wire wren_a, wren_b,
    output reg [31:0] q_a, q_b
);
    reg [31:0] memory [0:1023];
    integer i;
    initial begin
        q_a = 0;
        q_b = 0;
        for (i = 0; i < 1024; i = i + 1) memory[i] = 0;
    end
    always @(posedge clock_a) begin
        if (wren_a) memory[address_a] <= data_a;
        q_a <= memory[address_a];
    end
    always @(posedge clock_b) begin
        if (wren_b) memory[address_b] <= data_b;
        q_b <= memory[address_b];
    end
endmodule
