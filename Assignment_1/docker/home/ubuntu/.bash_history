#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/string.h>

/*
 * sys_revstr (nr = 451)
 *   參數: (char __user *str, size_t n)
 *   動作: 印出原字串 -> 反轉 -> 印出反轉字串 -> 寫回 user 空間 -> 回傳 0
 *   題目說可不特別處理無效引數，但 copy_{from,to}_user 失敗時回 -EFAULT
 */
SYSCALL_DEFINE2(revstr, char __user *, str, size_t, n)
{
    char *kbuf;
    size_t i = 0, j;

    if (!str || n == 0)
        return 0;

    kbuf = kmalloc(n + 1, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    if (copy_from_user(kbuf, str, n)) {
        kfree(kbuf);
        return -EFAULT;
    }
    kbuf[n] = '\0';

    pr_info("The origin string: %s\n", kbuf);

    j = n - 1;
    while (i < j) {
        char t = kbuf[i];
        kbuf[i] = kbuf[j];
        kbuf[j] = t;
        i++;
        j--;
    }

    pr_info("The reversed string: %s\n", kbuf);

    if (copy_to_user(str, kbuf, n)) {
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);
    return 0;
}

/*
 * sys_tempbuf (nr = 452)
 *   參數: (int mode, void __user *data, size_t size)
 *   模式:
 *     ADD    : 加到 list 末端，印 [tempbuf] Added: <data>，成功回 0
 *     REMOVE : 移除第一個匹配，印 [tempbuf] Removed: <data>；找不到回 -ENOENT
 *     PRINT  : 以空白串接所有字串，複製回 data（最多 size bytes），回傳實際複製字元數（不含 '\0'）
 *   失敗:
 *     -EFAULT: user 指標無效（data==NULL / size==0 / copy_{from,to}_user 失敗）
 *
 *   註: 題目保證 PRINT 產出長度不會超過 512 bytes。
 */
enum mode { PRINT = 0, ADD = 1, REMOVE = 2 };

struct tb_node {
    struct list_head node;
    char *s;
    size_t len;
};

static LIST_HEAD(tb_head);
static DEFINE_MUTEX(tb_lock);

SYSCALL_DEFINE3(tempbuf, int, mode, void __user *, data, size_t, size)
{
    if (mode != ADD && mode != REMOVE && mode != PRINT)
        return -EINVAL;

    /* ADD / REMOVE: 先把 user 的字串複製進 kernel */
    if (mode == ADD || mode == REMOVE) {
        char *kstr;

        if (!data || size == 0)
            return -EFAULT;

        kstr = kmalloc(size + 1, GFP_KERNEL);
        if (!kstr)
            return -ENOMEM;

        if (copy_from_user(kstr, data, size)) {
            kfree(kstr);
            return -EFAULT;
        }
        kstr[size] = '\0';

        if (mode == ADD) {
            struct tb_node *n = kmalloc(sizeof(*n), GFP_KERNEL);
            if (!n) {
                kfree(kstr);
                return -ENOMEM;
            }
            n->s = kstr;
            n->len = size;

            mutex_lock(&tb_lock);
            list_add_tail(&n->node, &tb_head);
            mutex_unlock(&tb_lock);

            pr_info("[tempbuf] Added: %s\n", kstr);
            return 0;
        } else { /* REMOVE */
            struct tb_node *pos, *tmp;
            int found = 0;

            mutex_lock(&tb_lock);
            list_for_each_entry_safe(pos, tmp, &tb_head, node) {
                if (pos->len == size && !memcmp(pos->s, kstr, size)) {
                    list_del(&pos->node);
                    pr_info("[tempbuf] Removed: %s\n", pos->s);
                    kfree(pos->s);
                    kfree(pos);
                    found = 1;
                    break;
                }
            }
            mutex_unlock(&tb_lock);

            kfree(kstr);
            return found ? 0 : -ENOENT;
        }
    }

    /* PRINT: 串接 list 內容，以單一空白分隔，拷回 user，回傳拷回的實際字元數（不含 '\0'） */
    if (mode == PRINT) {
        size_t sum = 0, cnt = 0, need, cap, written = 0;
        struct tb_node *pos;
        char *out;

        if (!data || size == 0)
            return -EFAULT;

        /* 第一次遍歷計算總長度（字串總長 + 空白數） */
        mutex_lock(&tb_lock);
        list_for_each_entry(pos, &tb_head, node) {
            sum += pos->len;
            cnt++;
        }
        mutex_unlock(&tb_lock);

        if (cnt == 0) {
            /* 空清單：回傳空字串 */
            char zero = '\0';
            if (copy_to_user(data, &zero, 1))
                return -EFAULT;
            return 0;
        }

        need = sum + (cnt - 1);                 /* 理論需要長度（不含 '\0'） */
        cap  = (need > 512) ? 512 : need;       /* 題目保證不超過 512，這裡仍防呆 */

        out = kmalloc(cap + 1, GFP_KERNEL);
        if (!out)
            return -ENOMEM;

        /* 第二次遍歷實際拷貝，注意容量 cap */
        {
            size_t idx = 0;
            size_t i = 0;

            mutex_lock(&tb_lock);
            list_for_each_entry(pos, &tb_head, node) {
                if (i++ > 0) {
                    if (idx < cap)
                        out[idx++] = ' ';
                }
                if (idx < cap) {
                    size_t can = pos->len;
                    if (can > cap - idx)
                        can = cap - idx;
                    if (can) {
                        memcpy(out + idx, pos->s, can);
                        idx += can;
                    }
                }
            }
            mutex_unlock(&tb_lock);

            out[idx] = '\0';
            written = idx; /* 不含 '\0' */
        }

        /* 連同結尾 '\0' 一起拷回（但回傳值不含 '\0'） */
        if (copy_to_user(data, out, (written + 1 <= size) ? (written + 1) : size)) {
            kfree(out);
            return -EFAULT;
        }

        kfree(out);
        return (int)written;
    }

    return 0;
}
EOF

wc -l kernel/os_hw1_syscalls.c
sed -n '1,200p' kernel/os_hw1_syscalls.c
git rev-parse --is-inside-work-tree
git config --local user.name  "CHENG-WEI SHIH"
git config --local user.email "william0726.tai@gmail.com"
git status -s
git add kernel/os_hw1_syscalls.c
git commit -s -m "riscv: add custom syscalls revstr(451) and tempbuf(452)"
git add arch/riscv/include/uapi/asm/unistd.h         arch/riscv/kernel/compat_syscall_table.c         arch/riscv/kernel/syscall_table.c         kernel/Makefile
git commit -s -m "riscv: wire up revstr/tempbuf in syscall tables and build"
git log --oneline -2
mkdir -p ~/HW1_313512009
git format-patch v6.1..HEAD   --output-directory ~/HW1_313512009   --subject-prefix="PATCH HW1"   --signoff
ls
cd ..
cd HW1_313512009/
ls
cd ../linux/
cp ~/linux/.config ~/HW1_<student ID>/
cp ~/linux/.config ~/HW1_313512009/
dpkg --get-selections > ~/HW1_313512009/packages_list_313512009.txt
cd ../HW1_313512009/
ls
cd ../linux/
cp ~/linux/.config ~/HW1_313512009/
cd ../HW1_313512009/
ls
docker ps
cd ..
docker ps
test -f /.dockerenv && echo "inside container" || echo "on host"
exit
ls
mkdir OS
cd OS
git clone https://github.com/torvalds/linux --branch v6.1 --depth 1 linux-recap
ls
rm linux-recap/
ls
rmdir linux-recap/
rm -rf linux-recap
ls
git clone https://github.com/torvalds/linux --branch v6.1 --depth 1
cd ..
ls
rm initramfs.cpio.gz 
rm -rf OS
ls
rm -rf ~/linux
clear
git clone https://github.com/torvalds/linux --branch v6.1 --depth 1 linux
cd linux
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
./scripts/config --set-str CONFIG_LOCALVERSION "-os-313512009"
./scripts/config --disable CONFIG_LOCALVERSION_AUTO
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- prepare
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- kernelrelease
make -j"$(nproc)" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu-
ls
cd ..
ls
ls -l ~/initramfs.cpio.gz
ls -l ~/initramfs.cpio.gz
qemu-system-riscv64 -nographic -machine virt   -kernel ~/linux/arch/riscv/boot/Image   -initrd ~/initramfs.cpio.gz   -append "console=ttyS0 loglevel=3"
qemu-system-riscv64 -nographic -machine virt   -kernel ~/linux/arch/riscv/boot/Image   -initrd ~/initramfs.cpio.gz   -append "console=ttyS0 loglevel=3"
ls
cd l
cd linux/
cd linu
nano kernel/os_hw1_syscalls.c
sudo apt-get update
sudo apt-get install -y nano
nano kernel/os_hw1_syscalls.c
nano kernel/os_hw1_syscalls.c
nano kernel/os_hw1_syscalls.c
wc -l kernel/os_hw1_syscalls.c
sed -n '1,200p' kernel/os_hw1_syscalls.c
grep -q 'os_hw1_syscalls.o' kernel/Makefile || echo 'obj-y += os_hw1_syscalls.o' >> kernel/Makefile
grep 'os_hw1_syscalls.o' -n kernel/Makefile
grep -Rn "sys_call_table" arch/riscv/kernel | head
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/kernel/compat_syscall_table.c
nano arch/riscv/include/uapi/asm/unistd.h
grep -n '__NR_syscalls' arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/include/uapi/asm/unistd.h
grep -n '__NR_revstr\|__NR_tempbuf\|__NR_syscalls' arch/riscv/include/uapi/asm/unistd.h
grep -n '__NR_revstr\|__NR_tempbuf\|__NR_syscalls' arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/include/uapi/asm/unistd.h
grep -n '\[451\]\|\[452\]' arch/riscv/kernel/syscall_table.c
grep -n '\[451\]\|\[452\]' arch/riscv/kernel/compat_syscall_table.c
make -j$(nproc) ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- Image
nano arch/riscv/kernel/syscall_table.c
make -j"$(nproc)" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- Image
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/kernel/compat_syscall_table.c
make -j"$(nproc)" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- Image
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/kernel/syscall_table.c
make -j"$(nproc)" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- Image
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/include/uapi/asm/unistd.h
sed -i 's/__SYSCALL(__NR_revstr,  sys_revstr)/__SYSCALL(__NR_revstr,  __riscv_sys_revstr)/'  arch/riscv/include/uapi/asm/unistd.h
sed -i 's/__SYSCALL(__NR_tempbuf, sys_tempbuf)/__SYSCALL(__NR_tempbuf, __riscv_sys_tempbuf)/' arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/include/uapi/asm/unistd.h
nl -ba arch/riscv/include/uapi/asm/unistd.h | sed -n '40,70p'
make -j"$(nproc)" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- Image
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/include/uapi/asm/unistd.h
make -j"$(nproc)" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- Image
nano arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/kernel/compat_syscall_table.c
make -j"$(nproc)" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- Image
grep -n 'os_hw1_syscalls\.o' kernel/Makefile
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- V=1 kernel/os_hw1_syscalls.o
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/kernel/compat_syscall_table.c
nano arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/kernel/compat_syscall_table.c

nano include/linux/syscalls.h
sudo sed -i '/^#endif[[:space:]]*\/\* _LINUX_SYSCALLS_H \*\/$/i \
/* HW1 custom syscalls */\
asmlinkage long sys_revstr(char __user *str, size_t n);\
asmlinkage long sys_tempbuf(int mode, void __user *data, size_t size);\
' include/linux/syscalls.h
grep -n 'sys_revstr\|sys_tempbuf' include/linux/syscalls.h
grep -n 'sys_revstr\|sys_tempbuf' include/linux/syscalls.h
nano include/linux/syscalls.h
nano include/linux/syscalls.h
nano include/linux/syscalls.h
nano arch/riscv/kernel/syscall_table.c
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- V=1 kernel/os_hw1_syscalls.o
nano arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/kernel/compat_syscall_table.c
nano kernel/os_hw1_syscalls.c 
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- kernel/os_hw1_syscalls.o
make -j"$(nproc)" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- Image
nano kernel/os_hw1_syscalls.c 
nano arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/kernel/compat_syscall_table.c
nano arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/include/uapi/asm/unistd.h
sed -i 's/__SYSCALL(__NR_revstr,  sys_revstr)/__SYSCALL(__NR_revstr,  __riscv_sys_revstr)/' arch/riscv/include/uapi/asm/unistd.h
sed -i 's/__SYSCALL(__NR_tempbuf, sys_tempbuf)/__SYSCALL(__NR_tempbuf, __riscv_sys_tempbuf)/' arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/kernel/syscall_table.c
sed -n '1,60p' arch/riscv/kernel/syscall_table.c
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/include/uapi/asm/unistd.h
nano kernel/os_hw1_syscalls.c 
nano kernel/Makefile
make -j"$(nproc)" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- Image
nano kernel/os_hw1_syscalls.c 
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/include/uapi/asm/unistd.h
nano arch/riscv/kernel/compat_syscall_table.c
cd ..
mkdir -p ~/hw1-tests
ls
cd hw1-tests/
cat > ~/hw1-tests/test_revstr.c << 'EOF'
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
EOF

cat > ~/hw1-tests/test_tempbuf.c << 'EOF'
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
EOF

ls
riscv64-linux-gnu-gcc -static -O2 -o test_revstr   test_revstr.c
riscv64-linux-gnu-gcc -static -O2 -o test_tempbuf  test_tempbuf.c
ls -lh
cd ,,
cd ..
mkdir -p initramfs && cd initramfs
ls
cd ...
cd ..
ls
cd initramfs
ls
gzip -dc ../initramfs.cpio.gz | cpio -idmv
cp ~/hw1-tests/test_revstr   ./usr/bin/
cp ~/hw1-tests/test_tempbuf  ./usr/bin/
find . | cpio -o -H newc | gzip > ../initramfs.cpio.gz
cd .
cd ..
ls
cd initramfs
ls
cd ..
qemu-system-riscv64 -nographic -machine virt   -kernel ~/linux/arch/riscv/boot/Image   -initrd ~/initramfs.cpio.gz   -append "console=ttyS0 loglevel=3"
qemu-system-riscv64 -nographic -machine virt   -kernel ~/linux/arch/riscv/boot/Image   -initrd ~/initramfs.cpio.gz   -append "console=ttyS0 loglevel=3"
qemu-system-riscv64 -nographic -machine virt   -kernel ~/linux/arch/riscv/boot/Image   -initrd ~/initramfs.cpio.gz   -append "console=ttyS0 loglevel=3"
nano kernel/os_hw1_syscalls.c
ls
cd linux/
nano kernel/os_hw1_syscalls.c
nano arch/riscv/include/uapi/asm/unistd.h
nano [200~arch/riscv/kernel/syscall_table.c~
ls
nano arch/riscv/kernel/syscall_table.c
nano arch/riscv/kernel/compat_syscall_table.c
exit
ls
cd HW1_313512009/
ls
cd ..
ls
ls -l ~/HW_313512009
ls -l ~/HW1_313512009
cp ~/linux/.config ~/HW1_313512009/.config
ls -l ~/HW1_313512009
zip -r HW1_313512009.zip HW1_313512009/
exit
