#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define __NR_revstr 451  // 你的 sys_revstr 系統呼叫號碼

int main(void) {
    char str1[20] = "hello";
    printf("Ori: %s\n", str1);
    int ret1 = syscall(__NR_revstr, str1, strlen(str1));
    assert(ret1 == 0);              // 確認回傳值為 0
    printf("Rev: %s\n", str1);      // 這邊應該被反轉成 "olleh"

    char str2[20] = "Operating System";
    printf("Ori: %s\n", str2);
    int ret2 = syscall(__NR_revstr, str2, strlen(str2));
    assert(ret2 == 0);
    printf("Rev: %s\n", str2);      // 這邊應該被反轉

    return 0;
}
