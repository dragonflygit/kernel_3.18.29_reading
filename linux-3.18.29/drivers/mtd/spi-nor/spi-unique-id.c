#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define OPCODE_WINBOND_ID	0x4b    /* Get WINBOND flash unique id */
#define UNIQUE_ID_SIZE		8
#define WINBOND_ID			0xEF	/* WINBOND manufacturer id */
#define MANUFACTURER_ID_OFFSET	16
#define MANUFACTURER_ID_MASK	0xFF

static u8 unique_id[UNIQUE_ID_SIZE];

static void *proc_start(struct seq_file *seq, loff_t *loff_pos)
{
	static unsigned long counter = 0;
	/* beginning a new sequence ? */
	if ( *loff_pos == 0 ) {
		/* yes => return a non null value to begin the sequence */
		return &counter;
	} else {
		/* no => it's the end of the sequence, return end to stop reading */
		*loff_pos = 0;
		return NULL;
	}
}

static void *proc_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return NULL;
}

static void proc_stop(struct seq_file *seq, void *v)
{
	//don't need to do anything
}

static int proc_show(struct seq_file *s, void *v)
{
	seq_printf(s, "id: %02x%02x%02x%02x%02x%02x%02x%02x",
			unique_id[7], unique_id[6], unique_id[5], unique_id[4],
			unique_id[3], unique_id[2], unique_id[1], unique_id[0]);
	return 0;
}

static struct seq_operations proc_sops = {
	.start = proc_start,
	.next  = proc_next,
	.stop  = proc_stop,
	.show  = proc_show
};

static int proc_open(struct inode *inode, struct file* file)
{
	return seq_open(file, &proc_sops);
}

static struct file_operations proc_fops = {
	.owner   = THIS_MODULE,
	.open    = proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

/* Used for Winbond flashes only */
static bool is_supported(u32 jedec_id)
{
	if (((jedec_id >> MANUFACTURER_ID_OFFSET) & MANUFACTURER_ID_MASK) == WINBOND_ID)
		return true;

	return false;
}

/* flash unique id probe */
void unique_id_probe(struct device *dev, u32 jedec_id)
{
	int ret;
	u8 code[5] = {0};
	struct spi_device *spi = to_spi_device(dev);

	if (!is_supported(jedec_id)) {
		dev_err(dev, "[0x%x] not support read UNIQUE ID!\n", jedec_id);
		return;
	}

	memset(unique_id, 0xff, UNIQUE_ID_SIZE);
	code[0] = OPCODE_WINBOND_ID;
	ret = spi_write_then_read(spi, &code, 5, unique_id, UNIQUE_ID_SIZE);
	if (ret < 0) {
		dev_err(dev, "error %d read UNIQUE ID\n", ret);
		return;
	}
	dev_dbg(dev, "flash unique id: %02x%02x%02x%02x%02x%02x%02x%02x\n",
			unique_id[7], unique_id[6], unique_id[5], unique_id[4],
			unique_id[3], unique_id[2], unique_id[1], unique_id[0]);

	proc_create("flash_unique_id",  0, NULL, &proc_fops);
	return;
}
