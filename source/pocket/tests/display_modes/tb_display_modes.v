// SPDX-License-Identifier: Apache-2.0
// Runs the real patched bridge + real video adapter on unrelated clocks.
`timescale 1ns/1ps
`default_nettype none
module tb_display_modes #(
    parameter integer BRIDGE_HALF_PERIOD = 7,
    parameter integer VIDEO_HALF_PERIOD = 20,
    parameter integer VIDEO_INITIAL_LEVEL = 0
);
    reg clk = 0;
    always #(BRIDGE_HALF_PERIOD) clk = ~clk;
    reg clk_vid = VIDEO_INITIAL_LEVEL;
    reg video_clock_run = 1;
    always #(VIDEO_HALF_PERIOD) if (video_clock_run) clk_vid = ~clk_vid;

    reg endian_little = 0;
    reg [31:0] bridge_addr = 0;
    reg bridge_rd = 0, bridge_wr = 0;
    reg [31:0] bridge_wr_data = 0;
    wire [31:0] bridge_rd_data;
    wire reset_n;
    wire [7:0] mode_id;
    wire grayscale_request;
    wire grayscale_applied;
    wire request_toggle, request_ack;
    reg [2:0] request_ack_sync = 0;
    wire in_menu;
    reg grayscale_applied_meta = 0;
    reg grayscale_applied_74a = 0;
    always @(posedge clk) begin
        request_ack_sync <= {request_ack_sync[1:0], request_ack};
        grayscale_applied_meta <= grayscale_applied;
        grayscale_applied_74a <= grayscale_applied_meta;
    end

    core_bridge_cmd #(.DISPLAY_MODE_TIMEOUT_CYCLES(24'd1000)) bridge (
        .clk(clk), .reset_n(reset_n),
        .bridge_endian_little(endian_little),
        .bridge_addr(bridge_addr), .bridge_rd(bridge_rd),
        .bridge_rd_data(bridge_rd_data), .bridge_wr(bridge_wr),
        .bridge_wr_data(bridge_wr_data),
        .shutdown_pending(), .shutdown_ack_s(1'b1),
        .status_boot_done(1'b1), .status_setup_done(1'b0),
        .status_running(reset_n),
        .dataslot_requestread(), .dataslot_requestread_id(),
        .dataslot_requestread_ack(1'b1), .dataslot_requestread_ok(1'b1),
        .dataslot_requestwrite(), .dataslot_requestwrite_id(),
        .dataslot_requestwrite_size(),
        .dataslot_requestwrite_ack(1'b1), .dataslot_requestwrite_ok(1'b1),
        .dataslot_update(), .dataslot_update_id(), .dataslot_update_size(),
        .dataslot_allcomplete(), .rtc_epoch_seconds(), .rtc_date_bcd(),
        .rtc_time_bcd(), .rtc_valid(),
        .savestate_supported(1'b0), .savestate_addr(32'd0),
        .savestate_size(32'd0), .savestate_maxloadsize(32'd0),
        .osnotify_inmenu(in_menu), .osnotify_display_mode(mode_id),
        .osnotify_grayscale(grayscale_request),
        .osnotify_display_request(request_toggle),
        .osnotify_display_ack(request_ack_sync[2]),
        .osnotify_grayscale_applied(grayscale_applied_74a),
        .savestate_start(), .savestate_start_ack(1'b0),
        .savestate_start_busy(1'b0), .savestate_start_ok(1'b0),
        .savestate_start_err(1'b0),
        .savestate_load(), .savestate_load_ack(1'b0),
        .savestate_load_busy(1'b0), .savestate_load_ok(1'b0),
        .savestate_load_err(1'b0),
        .target_dataslot_read(1'b0), .target_dataslot_write(1'b0),
        .target_dataslot_getfile(1'b0), .target_dataslot_openfile(1'b0),
        .target_dataslot_ack(), .target_dataslot_done(), .target_dataslot_err(),
        .target_dataslot_id(16'd0), .target_dataslot_slotoffset(32'd0),
        .target_dataslot_bridgeaddr(32'd0), .target_dataslot_length(32'd0),
        .target_buffer_param_struct(32'd0), .target_buffer_resp_struct(32'd0),
        .datatable_addr(10'd0), .datatable_wren(1'b0), .datatable_data(32'd0),
        .datatable_q()
    );

    reg [23:0] rgb_in = 0;
    reg de_in = 0, skip_in = 0, vs_in = 0, hs_in = 0;
    wire [23:0] rgb_out;
    wire de_out, skip_out, vs_out, hs_out;
    pal_display_mode_video video (
        .clk_vid(clk_vid), .grayscale_request_async(grayscale_request),
        .request_toggle_async(request_toggle),
        .scanout_reset_n(reset_n),
        .rgb_in(rgb_in), .de_in(de_in), .skip_in(skip_in),
        .vs_in(vs_in), .hs_in(hs_in), .rgb_out(rgb_out),
        .de_out(de_out), .skip_out(skip_out), .vs_out(vs_out), .hs_out(hs_out),
        .grayscale_applied(grayscale_applied), .request_ack_toggle(request_ack)
    );

    // Continuously check actual output. The reference uses full-width
    // integer multiplication, independent of the DUT's shift/add arithmetic.
    integer samples = 0, color_samples = 0, gray_samples = 0, blank_samples = 0;
    integer r, g, b, y;
    reg [23:0] expected_rgb;
    reg [3:0] expected_sidebands;
    reg previous_applied = 0;
    reg boundary;
    always @(posedge clk_vid) begin
        r = rgb_in[23:16]; g = rgb_in[15:8]; b = rgb_in[7:0];
        y = (77*r + 150*g + 29*b + 128) / 256;
        expected_rgb = rgb_in;
        expected_sidebands = {de_in, skip_in, vs_in, hs_in};
        boundary = !reset_n || (vs_in && !de_in);
        #1;
        if ({de_out, skip_out, vs_out, hs_out} !== expected_sidebands)
            $fatal(1, "APF sidebands not aligned to one-cycle RGB pipeline");
        if (grayscale_applied !== previous_applied && !boundary)
            $fatal(1, "Display mode changed inside a frame");
        previous_applied = grayscale_applied;
        if (de_out && grayscale_applied) begin
            expected_rgb = {y[7:0], y[7:0], y[7:0]};
            gray_samples = gray_samples + 1;
        end else if (de_out) color_samples = color_samples + 1;
        else blank_samples = blank_samples + 1;
        if (rgb_out !== expected_rgb)
            $fatal(1, "Pixel/control mismatch got=%06x expected=%06x", rgb_out, expected_rgb);
        samples = samples + 1;
    end

    function [31:0] bus_word;
        input [31:0] word;
        begin
            bus_word = endian_little ? {word[7:0],word[15:8],word[23:16],word[31:24]} : word;
        end
    endfunction
    task write_word;
        input [31:0] addr, value;
        begin
            @(negedge clk); bridge_addr = addr;
            bridge_wr_data = bus_word(value); bridge_wr = 1;
            @(negedge clk); bridge_wr = 0;
        end
    endtask
    task read_word;
        input [31:0] addr;
        output [31:0] value;
        begin
            @(negedge clk); bridge_addr = addr; bridge_rd = 1;
            @(posedge clk); #1; value = bus_word(bridge_rd_data);
            @(negedge clk); bridge_rd = 0;
        end
    endtask
    task start_command;
        input [15:0] command;
        input [31:0] param;
        begin
            write_word(32'hf8000020, param);
            write_word(32'hf8000000, {16'h434d,command});
        end
    endtask
    task wait_result;
        input [31:0] expected;
        integer count;
        reg [31:0] value;
        begin : poll
            for (count = 0; count < 600; count = count + 1) begin
                read_word(32'hf8000000, value);
                if (value[31:16] == 16'h4f4b) begin
                    if (value !== expected) $fatal(1, "Bad status %08x, expected %08x", value, expected);
                    disable poll;
                end
            end
            $fatal(1, "Bridge failed to terminate within bounded test timeout");
        end
    endtask
    task check_response;
        input [31:0] expected;
        reg [31:0] value;
        begin
            read_word(32'hf8000008, value);
            if (value !== 32'h40) $fatal(1, "Wrong host response pointer");
            read_word(32'hf8000040, value);
            if (value !== expected) $fatal(1, "Wrong response data %08x expected %08x", value, expected);
        end
    endtask
    task frame_boundary;
        begin
            @(negedge clk_vid); rgb_in = 24'h000000;
            vs_in = 1; de_in = 0; hs_in = 0; skip_in = 0;
            @(negedge clk_vid); vs_in = 0;
        end
    endtask
    task notify;
        input [31:0] param;
        begin
            start_command(16'h00b8, param);
            repeat (8) @(negedge clk_vid);
            frame_boundary;
            wait_result(32'h4f4b0000);
            if (mode_id !== param[15:8] || grayscale_request !== param[0])
                $fatal(1, "Mode or grayscale request was not latched exactly");
            if (grayscale_applied !== param[0]) $fatal(1, "ACK before video applied");
            check_response(param[0] ? 32'h444d : 32'h0);
        end
    endtask
    task pixel;
        input [23:0] value;
        input [3:0] controls;
        begin
            @(negedge clk_vid); rgb_in = value;
            {de_in, skip_in, vs_in, hs_in} = controls;
            @(posedge clk_vid); #2;
        end
    endtask

    task drain_recovery;
        integer n;
        begin
            // One frame may apply the canceled transaction; a later frame
            // applies its explicit rollback. No success is allowed between.
            for (n = 0; n < 4; n = n + 1) begin
                repeat (12) @(negedge clk_vid);
                frame_boundary;
            end
            repeat (10) @(negedge clk);
            if (bridge.display_recovery !== 0)
                $fatal(1, "Canceled transaction did not drain and roll back");
        end
    endtask
    task near_timeout;
        input initial_gray;
        input integer edge_margin;
        integer n;
        reg [31:0] status;
        begin
            notify(initial_gray ? 32'h2001 : 32'h1000);
            pixel(24'hff0000, 4'b1000);
            start_command(16'h00b8, initial_gray ? 32'h1000 : 32'h2001);
            wait (bridge.display_mode_wait_cycles >= 1000 - edge_margin);
            frame_boundary;
            begin : poll_near
                for (n = 0; n < 600; n = n + 1) begin
                    read_word(32'hf8000000, status);
                    if (status[31:16] == 16'h4f4b) disable poll_near;
                end
                $fatal(1, "Near-timeout command never terminated");
            end
            if (status == 32'h4f4b0000) begin
                if (grayscale_applied !== !initial_gray)
                    $fatal(1, "Near-timeout success acknowledged the wrong transaction");
                check_response(initial_gray ? 0 : 32'h444d);
            end else if (status == 32'h4f4bffff) begin
                check_response(0);
                // Reject a new request while cancellation/rollback is still
                // outstanding, even when its requested value matches old ACK.
                start_command(16'h00b8, initial_gray ? 32'h2001 : 32'h1000);
                wait_result(32'h4f4bffff); check_response(0);
                drain_recovery;
                if (grayscale_applied !== initial_gray)
                    $fatal(1, "Near-timeout rollback restored wrong grayscale state");
            end else $fatal(1, "Unexpected near-timeout status %08x", status);
            notify(initial_gray ? 32'h2001 : 32'h1000);
        end
    endtask

    integer i;
    reg [31:0] value;
    reg [31:0] rng = 32'h5a3c7e91;
    initial begin
        #200;
        if (reset_n !== 0 || mode_id !== 0 || grayscale_request !== 0)
            $fatal(1, "Initial display/reset state is not deterministic");
        // Boot-time notify while the game is held in reset and has no VS.
        start_command(16'h00b8, 32'h00002001);
        wait_result(32'h4f4b0000); check_response(32'h444d);
        if (!grayscale_applied) $fatal(1, "Reset-time ACK before grayscale application");
        start_command(16'h0011, 0); wait_result(32'h4f4b0000);

        // No mid-frame switching, and response MUST wait for actual video.
        notify(32'h00001000);
        pixel(24'hf013b7, 4'b1000);
        start_command(16'h00b8, 32'h00002301);
        repeat (12) @(negedge clk_vid);
        read_word(32'hf8000000, value);
        if (value[31:16] !== 16'h4255 || grayscale_applied !== 0)
            $fatal(1, "Command acknowledged or changed output before VS");
        check_response(32'h0);
        frame_boundary; wait_result(32'h4f4b0000); check_response(32'h444d);

        // All neutral levels are exact; endpoints use the full 0..255 range.
        for (i = 0; i < 256; i = i + 1) pixel({i[7:0],i[7:0],i[7:0]}, 4'b1000);
        pixel(24'hff0000, 4'b1000);
        pixel(24'h00ff00, 4'b1000);
        pixel(24'h0000ff, 4'b1000);
        for (i = 0; i < 4096; i = i + 1) begin
            rng = (rng * 32'd1664525) + 32'd1013904223;
            pixel(rng[23:0], 4'b1000 | ((i & 1) ? 4'b0100 : 0));
        end
        // Every scaler-slot end-of-line word survives byte-for-byte.
        for (i = 0; i < 8; i = i + 1) pixel({8'd0,i[2:0],13'd0}, 4'b0001);
        // Frame flags and arbitrary blanking payloads must not be converted.
        for (i = 0; i < 256; i = i + 1) begin
            rng = (rng * 32'd1664525) + 32'd1013904223;
            pixel(rng[23:0], {1'b0,i[2:0]});
        end

        // Warm reset preserves the last display selection, and does not
        // require a new 00B8 to keep the selected monochrome mode working.
        start_command(16'h0010, 0); wait_result(32'h4f4b0000);
        if (mode_id !== 8'h23 || !grayscale_request || !grayscale_applied)
            $fatal(1, "Warm reset forgot the selected display mode");
        start_command(16'h0011, 0); wait_result(32'h4f4b0000);
        notify(32'h00000000); // normal mode restores exact color
        for (i = 0; i < 1024; i = i + 1) begin
            rng = (rng * 32'd1664525) + 32'd1013904223;
            pixel(rng[23:0], 4'b1000);
        end
        notify(32'h00003000); // reflective color
        pixel(24'hf013b7, 4'b1000);
        notify(32'h00004000); // backlit color
        pixel(24'h12fb3c, 4'b1000);
        // The request bit is authoritative, including future mode IDs.
        notify(32'hffff1001);
        pixel(24'hff0000, 4'b1000);
        notify(32'hffff2000);
        pixel(24'hff0000, 4'b1000);
        notify(32'h00002001);
        notify(32'h00002001); // repeated notification
        notify(32'h00000000);
        start_command(16'h00b0, 1); wait_result(32'h4f4b0000);
        if (!in_menu || mode_id !== 0 || grayscale_request !== 0)
            $fatal(1, "Menu notification corrupted display state");
        start_command(16'h00b1, 0); wait_result(32'h4f4b0000);
        start_command(16'h00b2, 1); wait_result(32'h4f4b0000);

        endian_little = 1;
        repeat (10) @(negedge clk);
        notify(32'h00002201);
        pixel(24'h0000ff, 4'b1000);
        notify(32'h00001000);

        // No VS: fail honestly, keeping the in-flight mailbox immutable.
        pixel(24'h234567, 4'b1000);
        start_command(16'h00b8, 32'h00002101);
        wait_result(32'h4f4bffff); check_response(0);
        if (mode_id !== 8'h10 || grayscale_applied !== 0 || grayscale_request !== 1)
            $fatal(1, "Timeout mutated in-flight payload or committed failed mode");
        drain_recovery;
        if (grayscale_applied !== 0 || grayscale_request !== 0)
            $fatal(1, "Canceled request did not restore prior color state");

        // Regression: stop AFTER the video domain captures the gray request.
        // A subsequent color request must not falsely succeed from old
        // applied=0 while canceled gray is still pending in the video CDC.
        pixel(24'hff0000, 4'b1000);
        start_command(16'h00b8, 32'h00002001);
        repeat (6) @(negedge clk_vid);
        video_clock_run = 0;
        wait_result(32'h4f4bffff); check_response(0);
        if (mode_id !== 8'h10 || grayscale_request !== 1 || grayscale_applied !== 0)
            $fatal(1, "Stopped-clock timeout did not freeze canceled mailbox");
        start_command(16'h00b8, 32'h00000000);
        wait_result(32'h4f4bffff); check_response(0);
        video_clock_run = 1;
        frame_boundary;
        repeat (10) @(negedge clk_vid);
        pixel(24'hff0000, 4'b1000);
        if (grayscale_applied !== 1 || rgb_out !== 24'h4d4d4d)
            $fatal(1, "Test did not exercise late canceled grayscale application");
        start_command(16'h00b8, 32'h00000000);
        wait_result(32'h4f4bffff); check_response(0);
        drain_recovery;
        notify(32'h00000000);
        pixel(24'hff0000, 4'b1000);
        repeat (2) begin frame_boundary; pixel(24'hff0000, 4'b1000); end
        if (grayscale_applied !== 0 || rgb_out !== 24'hff0000)
            $fatal(1, "Stale canceled request changed pixels after color success");

        // Stopping before the request can cross also cannot alias toggles.
        @(negedge clk_vid); video_clock_run = 0;
        start_command(16'h00b8, 32'h00002001);
        wait_result(32'h4f4bffff); check_response(0);
        start_command(16'h00b8, 32'h00000000);
        wait_result(32'h4f4bffff); check_response(0);
        video_clock_run = 1;
        drain_recovery;
        notify(32'h00000000);

        // Sweep the timeout edge in BOTH directions. Fresh tagged ACK either
        // succeeds correctly, or cancellation fully drains before new work.
        for (i = 1; i <= 8; i = i + 1) begin
            near_timeout(1'b0, i);
            near_timeout(1'b1, i);
        end
        notify(32'h00000000);
        pixel(24'habcdef, 4'b1000);
        if (gray_samples < 4355 || color_samples < 1028 || blank_samples < 264)
            $fatal(1, "Insufficient coverage");
        $display("PASS display modes: %0d samples (%0d gray, %0d color, %0d blank/control)",
                 samples, gray_samples, color_samples, blank_samples);
        $display("PASS 00B8 both endians; boot/reset; frame latch; mode/request; 444D; tagged cancellation/rollback; timeout-edge sweep");
        $finish;
    end
    initial begin
        #2000000;
        $fatal(1, "Global test timeout");
    end
endmodule
`default_nettype wire
