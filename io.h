#ifndef _NOSDK_IO_H
#define _NOSDK_IO_H

struct nosdk_io_mgr {
    int num;
};

int nosdk_io_mgr_init(struct nosdk_io_mgr *mgr);

#endif // _NOSDK_IO_H
