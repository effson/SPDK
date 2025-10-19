#include <stdio.h>
#include <spdk/event.h>
#include <spdk/blob_bdev.h>
#include <spdk/blob.h>
#include <spdk/bdev.h>
#include <spdk/env.h>

typedef struct jeff_fs_context_s {
    struct spdk_bs_dev *bs_dev;
    struct spdk_blob_store *blb_store;
    spdk_blob_id blb_id;
    struct spdk_io_channel *channel;
    struct spdk_blob *blb;

    uint64_t io_unit_size;
    uint8_t *write_buf;
    uint8_t *read_buf;
} jeff_fs_context_t;


static void jeff_fs_bs_unload_complete_cb(void *cb_arg, int bserrno) {
    // jeff_fs_context_t *ctx = (jeff_fs_context_t *)cb_arg;

    spdk_app_stop(1);
}


static void jeff_fs_bs_unload(jeff_fs_context_t *ctx) {
    if (ctx->blb_store) {
        if (ctx->channel) {
            spdk_bs_free_io_channel(ctx->channel);
        }

        if (ctx->write_buf) {
            spdk_free(ctx->write_buf);
            ctx->write_buf = NULL;
        }

        if (ctx->read_buf) {
            spdk_free(ctx->read_buf);
            ctx->read_buf = NULL;
        }

        spdk_bs_unload(ctx->blb_store, jeff_fs_bs_unload_complete_cb, ctx);
    }   
}

static void jeff_fs_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev,
            void *event_ctx) {
    SPDK_NOTICELOG("---> Enter %s\n", __func__);
}

static void jeff_fs_blob_read_complete_cb(void *cb_arg, int bserrno){
    jeff_fs_context_t *ctx = (jeff_fs_context_t *)cb_arg;
    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    SPDK_NOTICELOG("size:%lu, buf:%s\n", ctx->io_unit_size, ctx->read_buf);
}

static void jeff_fs_blob_write_complete_cb(void *cb_arg, int bserrno){
    jeff_fs_context_t *ctx = (jeff_fs_context_t *)cb_arg;
    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    ctx->read_buf = spdk_zmalloc(ctx->io_unit_size, 0x1000, NULL,
                                 SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
    if (!ctx->read_buf) {
        SPDK_NOTICELOG("Failed to allocate read buffer\n");
        jeff_fs_bs_unload(ctx);
        spdk_app_stop(-1);
        return;
    }

    spdk_blob_io_read(ctx->blb, ctx->channel, ctx->read_buf, 0, 1, jeff_fs_blob_read_complete_cb, ctx);
}

static void jeff_fs_blob_sync_complete_cb(void *cb_arg, int bserrno){
    jeff_fs_context_t *ctx = (jeff_fs_context_t *)cb_arg;

    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    ctx->write_buf = spdk_zmalloc(ctx->io_unit_size, 0x1000, NULL,
                                  SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
    if (!ctx->write_buf) {
        SPDK_NOTICELOG("Failed to allocate write buffer\n");
        jeff_fs_bs_unload(ctx);
        spdk_app_stop(-1);
        return;
    }
    memset(ctx->write_buf, '\0', ctx->io_unit_size);
    memset(ctx->write_buf, 'A', ctx->io_unit_size - 1);

    struct spdk_io_channel *channel = spdk_bs_alloc_io_channel(ctx->blb_store);
    if (!channel) {
        SPDK_NOTICELOG("Failed to allocate I/O channel\n");

        // destroy jeff_fs_context_t
        spdk_app_stop(-1);
        return;
    }

    ctx->channel = channel;
    spdk_blob_io_write(ctx->blb, channel, ctx->write_buf, 0, 1, jeff_fs_blob_write_complete_cb, ctx);
}

static void jeff_fs_blob_resize_complete_cb(void *cb_arg, int bserrno){
    jeff_fs_context_t *ctx = (jeff_fs_context_t *)cb_arg;

    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    
    spdk_blob_sync_md(ctx->blb, jeff_fs_blob_sync_complete_cb, ctx);
}

static void jeff_fs_blob_open_complete_cb(void *cb_arg, struct spdk_blob *blb, int bserrno){
    jeff_fs_context_t *ctx = (jeff_fs_context_t *)cb_arg;

    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    ctx->blb = blb;
    uint64_t total = spdk_bs_free_cluster_count(ctx->blb_store);
    spdk_blob_resize(blb, total, jeff_fs_blob_resize_complete_cb, ctx);
}

static void jeff_fs_bs_create_complete_cb(void *cb_arg, spdk_blob_id blobid, int bserrno){
    jeff_fs_context_t *ctx = (jeff_fs_context_t *)cb_arg;

    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    ctx->blb_id = blobid;

    spdk_bs_open_blob(ctx->blb_store, blobid, jeff_fs_blob_open_complete_cb, ctx);
}

static void jeff_fs_bs_init_complete_cb(void *cb_arg, struct spdk_blob_store *bs,
		int bserrno) {
    jeff_fs_context_t *ctx = (jeff_fs_context_t *)cb_arg;
    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    ctx->blb_store = bs;
    ctx->io_unit_size = spdk_bs_get_io_unit_size(bs);
    spdk_bs_create_blob(bs, jeff_fs_bs_create_complete_cb, ctx);
}

static void jeff_fs_entry (void *arg) {
    jeff_fs_context_t *ctx = (jeff_fs_context_t *)arg;

    SPDK_NOTICELOG("---> Enter %s\n", __func__);

    const char *bdev_name = "Malloc0";
    // struct spdk_bs_dev *bs_dev = NULL;
    int rc = spdk_bdev_create_bs_dev_ext(bdev_name, jeff_fs_bdev_event_cb, NULL, &ctx->bs_dev);
    if (rc != 0) {
        SPDK_NOTICELOG("Failed to create blobstore on bdev %s\n", bdev_name);
        spdk_app_stop(-1);
        return;
    }

    spdk_bs_init(ctx->bs_dev, NULL, jeff_fs_bs_init_complete_cb, ctx);
}

int main(int argc, char *argv[]) {

    struct spdk_app_opts opts = {};
    spdk_app_opts_init(&opts, sizeof(struct spdk_app_opts));
    opts.name = "jeff-fs";
    opts.json_config_file = argv[1];

    jeff_fs_context_t *ctx = calloc(1, sizeof(jeff_fs_context_t));
    if (!ctx) {
        SPDK_NOTICELOG("Failed to allocate jeff_fs_context_t\n");
        return -1;
    }

    int ret = spdk_app_start(&opts, jeff_fs_entry, ctx);
    if (ret) {
        SPDK_NOTICELOG("ERROR : app start failed!\n");
    } else {
        SPDK_NOTICELOG("SUCCESS : app start!\n");
    }

    return 0;
}
