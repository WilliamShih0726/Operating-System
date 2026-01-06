#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#define __NR_tempbuf 452

enum mode {
    PRINT,
    ADD,
    REMOVE
};

int main(void) {
    char buf[512];
    int ret;

    // 新增兩筆
    ret = syscall(__NR_tempbuf, ADD, "Hello", (size_t)strlen("Hello"));
    assert(ret == 0);

    ret = syscall(__NR_tempbuf, ADD, "Operating Systems", (size_t)strlen("Operating Systems"));
    assert(ret == 0);

    // 印出全部（預期：Hello Operating Systems）
    memset(buf, 0, sizeof(buf));
    ret = syscall(__NR_tempbuf, PRINT, buf, sizeof(buf));
    assert(ret >= 0);
    printf("%s\n", buf);

    // 刪除不存在的字串 -> 應回傳 -1 並設 errno=ENOENT
    errno = 0;
    ret = syscall(__NR_tempbuf, REMOVE, "NotExist", (size_t)strlen("NotExist"));
    assert(ret == -1 && errno == ENOENT);

    // 刪除 "Hello"
    ret = syscall(__NR_tempbuf, REMOVE, "Hello", (size_t)strlen("Hello"));
    assert(ret == 0);

    // 再印一次（預期：Operating Systems）
    memset(buf, 0, sizeof(buf));
    ret = syscall(__NR_tempbuf, PRINT, buf, sizeof(buf));
    assert(ret >= 0);
    printf("%s\n", buf);

    return 0;
}
