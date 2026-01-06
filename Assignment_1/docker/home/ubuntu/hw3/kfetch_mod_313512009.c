#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/utsname.h>     // hostname / release
#include <linux/mm.h>          // si_meminfo()
#include <linux/sysinfo.h>     // struct sysinfo
#include <linux/cpu.h>         // num_online_cpus()
#include <linux/sched/signal.h> // for_each_process
#include <linux/rcupdate.h>    // rcu_read_lock()
#include <linux/timekeeping.h> // ktime_get_boottime_seconds()
#include "kfetch.h"
#define DEVICE_NAME KFETCH_DEV_NAME

#define KFETCH_BUF_SIZE 1024

struct kfetch_dev {
    dev_t devt;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct mutex lock;
    int mask;
};

static struct kfetch_dev kdev;

static int kfetch_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static int kfetch_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static void kfetch_get_meminfo(long *total_mb, long *free_mb)
{
    struct sysinfo info;

    si_meminfo(&info);
    // totalram / freeram 以「頁」為單位，mem_unit = PAGE_SIZE
    *total_mb = (info.totalram * info.mem_unit) >> 20; // /1024/1024
    *free_mb  = (info.freeram  * info.mem_unit) >> 20;
}

static long kfetch_get_uptime_mins(void)
{
    u64 sec = ktime_get_boottime_seconds();

    return (long)(sec / 60);
}

static long kfetch_get_num_procs(void)
{
    long count = 0;
    struct task_struct *p;

    rcu_read_lock();
    for_each_process(p)
        count++;
    rcu_read_unlock();

    return count;
}

/* CPU model：先給一個簡單版，之後你可以參考 arch/riscv/kernel/cpu.c 進一步改 */
static const char *kfetch_get_cpu_model(void)
{
    return "RISC-V virt CPU";
}

static int kfetch_build_output(char *buf, size_t size)
{
    int len = 0;
    int mask;
    const struct new_utsname *u = &init_uts_ns.name;
    const char *hostname = u->nodename;
    const char *release  = u->release;
    int hlen = strnlen(hostname, 64);

    int online_cpus = num_online_cpus();
    int total_cpus  = num_possible_cpus();
    long total_mb, free_mb;
    long uptime_mins = kfetch_get_uptime_mins();
    long num_procs   = kfetch_get_num_procs();
    const char *cpu_model = kfetch_get_cpu_model();
    int i;

    mutex_lock(&kdev.lock);
    mask = kdev.mask;
    mutex_unlock(&kdev.lock);

    /* 1. logo */
    len += scnprintf(buf + len, size - len,
        "        .-.        \n"
        "       (.. |       \n"
        "       <>  |       \n"
        "      / --- \\\\      \n"
        "     ( |   | )     \n"
        "   |\\\\_)__(_//|   \n"
        "  <__)------(__>   \n");

    /* 2. hostname（一定要顯示） */
    len += scnprintf(buf + len, size - len, "%s\n", hostname);

    /* 3. 分隔線：長度 = hostname 長度 */
    for (i = 0; i < hlen && len < size - 1; i++)
        buf[len++] = '-';
    buf[len++] = '\n';

    /* 4. 依 mask 顯示資訊 */

    if (mask & KFETCH_RELEASE) {
        len += scnprintf(buf + len, size - len,
                         "Kernel: %s\n", release);
    }

    if (mask & KFETCH_CPU_MODEL) {
        len += scnprintf(buf + len, size - len,
                         "CPU:    %s\n", cpu_model);
    }

    if (mask & KFETCH_NUM_CPUS) {
        len += scnprintf(buf + len, size - len,
                         "CPUs:   %d / %d\n",
                         online_cpus, total_cpus);
    }

    if (mask & KFETCH_MEM) {
        kfetch_get_meminfo(&total_mb, &free_mb);
        len += scnprintf(buf + len, size - len,
                         "Mem:    %ld / %ld MB\n",
                         free_mb, total_mb);
    }

    if (mask & KFETCH_NUM_PROCS) {
        len += scnprintf(buf + len, size - len,
                         "Procs:  %ld\n", num_procs);
    }

    if (mask & KFETCH_UPTIME) {
        len += scnprintf(buf + len, size - len,
                         "Uptime: %ld mins\n", uptime_mins);
    }

    return len;
}

static ssize_t kfetch_read(struct file *filp, char __user *buf,
                           size_t count, loff_t *ppos)
{
    char kbuf[KFETCH_BUF_SIZE];
    int len;

    len = kfetch_build_output(kbuf, sizeof(kbuf));

    return simple_read_from_buffer(buf, count, ppos, kbuf, len);
}

static ssize_t kfetch_write(struct file *filp, const char __user *buf,
                            size_t count, loff_t *ppos)
{
    int mask;

    if (count < sizeof(int))
        return -EINVAL;

    if (copy_from_user(&mask, buf, sizeof(int)))
        return -EFAULT;

    mutex_lock(&kdev.lock);

    if (mask == 0)
        kdev.mask = KFETCH_FULL_INFO;          // 預設顯示全部
    else
        kdev.mask = mask & KFETCH_FULL_INFO;   // 只保留合法 bit

    mutex_unlock(&kdev.lock);

    return sizeof(int);
}


static const struct file_operations kfetch_fops = {
    .owner   = THIS_MODULE,
    .open    = kfetch_open,
    .release = kfetch_release,
    .read    = kfetch_read,
    .write   = kfetch_write,
};

static int __init kfetch_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&kdev.devt, 0, 1, DEVICE_NAME);
    if (ret)
        return ret;

    cdev_init(&kdev.cdev, &kfetch_fops);
    kdev.cdev.owner = THIS_MODULE;

    ret = cdev_add(&kdev.cdev, kdev.devt, 1);
    if (ret)
        goto err_cdev;

    kdev.class = class_create(DEVICE_NAME);
    if (IS_ERR(kdev.class)) {
        ret = PTR_ERR(kdev.class);
        goto err_class;
    }

    kdev.device = device_create(kdev.class, NULL, kdev.devt, NULL, DEVICE_NAME);
    if (IS_ERR(kdev.device)) {
        ret = PTR_ERR(kdev.device);
        goto err_device;
    }

    mutex_init(&kdev.lock);
    kdev.mask = KFETCH_FULL_INFO;  /* 之後再用 */

    pr_info("kfetch_mod_313512009 loaded\n");
    return 0;

err_device:
    class_destroy(kdev.class);
err_class:
    cdev_del(&kdev.cdev);
err_cdev:
    unregister_chrdev_region(kdev.devt, 1);
    return ret;
}

static void __exit kfetch_exit(void)
{
    device_destroy(kdev.class, kdev.devt);
    class_destroy(kdev.class);
    cdev_del(&kdev.cdev);
    unregister_chrdev_region(kdev.devt, 1);

    pr_info("kfetch_mod_313512009 unloaded\n");
}

module_init(kfetch_init);
module_exit(kfetch_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("313512009");
MODULE_DESCRIPTION("kfetch system info module (skeleton)");
