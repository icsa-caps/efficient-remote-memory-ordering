ADDR_LIMIT = 1024 * 1024 * 1024

def gen_trace_line(addr, rsize, op, data):
    return hex(addr) + ',' + str(rsize) + ',' + op + ',' + str(data) + '\n'

def generate_linear_dma_trace(out_dir, req_size, num_reqs, isRead):
    assert ((num_reqs * req_size) < ADDR_LIMIT), "Not generating request to inaccessible memory\n"
    out_fname = out_dir + ("read-test-" if isRead else "write-test-") + "size" + str(req_size) + ".txt"
    addr = 0
    op = "RDMA_RD" if isRead else "RDMA_WR"
    with open(out_fname, 'w') as out_file:
        for i in range(num_reqs):
            line = gen_trace_line(addr, req_size, op, 0)
            out_file.write(line)
            addr = addr + req_size

def generate_repeat_dma_trace(out_dir, req_size, num_reqs, start_end, isRead):
    assert (start_end[0] < start_end[1]), "Address range should be sensible\n"
    assert (start_end[1] < ADDR_LIMIT), "Not generating request to inaccessible memory\n"
    out_fname = out_dir + "repeat-" + str(start_end[0]) + '-' + str(start_end[1]) + '-' + ("read-test-" if isRead else "write-test-") + "size" + str(req_size) + ".txt"
    addr = start_end[0]
    op = "RDMA_RD" if isRead else "RDMA_WR"
    with open(out_fname, 'w') as out_file:
        for i in range(num_reqs):
            line = gen_trace_line(addr, req_size, op, 0)
            out_file.write(line)
            addr = addr + req_size
            if (addr > start_end[1]):
                addr = start_end[0]

def gen_loop():
    for i in range(6, 14):
        req_size = 2 ** i
        generate_linear_dma_trace("../trc/read-dma-traces/", req_size, 1000, True)

gen_loop()

# generate_linear_dma_trace("../trc/write-dma-traces/", 128, 1000, False)

# generate_repeat_dma_trace("../trc/read-dma-traces/", 128, 3000, (0,255), True)

