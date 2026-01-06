// kernel/os_hw1_syscalls.c
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/errno.h>

/*
 * sys_revstr (nr = 451)
 *   參數: (char __user *str, size_t n)
 *   動作: 印出原字串 -> 反轉 -> 印出反轉字串 -> 寫回 user 空間 -> 回傳 0
 *   題目說可不特別處理無效引數，但 copy_{from,to}_user 失敗時回 -EFAULT
 */
asmlinkage long __riscv_sys_revstr(char __user *str, size_t n)
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

asmlinkage long __riscv_sys_tempbuf(int mode, void __user *data, size_t size)
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
