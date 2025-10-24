#include <stdio.h>
#include <spdk/event.h>
#include <spdk/blob_bdev.h>
#include <spdk/blob.h>
#include <spdk/bdev.h>
#include <spdk/env.h>
#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/init.h>

typedef struct jeff_fs_context_s {
    struct spdk_bs_dev *bs_dev;
    struct spdk_blob_store *blb_store;
    spdk_blob_id blb_id;
    struct spdk_io_channel *channel;
    struct spdk_blob *blb;

    uint64_t io_unit_size;
    uint8_t *write_buf;
    uint8_t *read_buf;

    bool completed;
} jeff_fs_context_t;

struct spdk_thread *global_thread = NULL;

static const int POLLER_MAX_TIME = 100000;

static bool poller(struct spdk_thread *thread, spdk_msg_fn fn, void *ctx, bool *completed) {
    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    spdk_thread_send_msg(thread, fn, ctx);
    
    int time_spent = 0;
    while (!(*completed) && time_spent < POLLER_MAX_TIME) {
        spdk_thread_poll(thread, 0, 0);
        time_spent++;
    }
    
    if (!(*completed) && time_spent >= POLLER_MAX_TIME) {
        return false;
    }
    SPDK_NOTICELOG("---> leave %s\n", __func__);
    return true;
}

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

static void jeff_fs_blob_sync_complete_cb(void *cb_arg, int bserrno){
    jeff_fs_context_t *ctx = (jeff_fs_context_t *)cb_arg;

#if 0

    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    ctx->write_buf = spdk_zmalloc(ctx->io_unit_size, 0x1000, NULL,
                                  SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
    if (!ctx->write_buf) {
        SPDK_NOTICELOG("Failed to allocate write buffer\n");
        jeff_fs_bs_unload(ctx);
        return;
    }
    memset(ctx->write_buf, '\0', ctx->io_unit_size);
    memset(ctx->write_buf, 'A', ctx->io_unit_size - 1);

    struct spdk_io_channel *channel = spdk_bs_alloc_io_channel(ctx->blb_store);
    if (!channel) {
        jeff_fs_bs_unload(ctx);
        return;
    }

    ctx->channel = channel;
    spdk_blob_io_write(ctx->blb, channel, ctx->write_buf, 0, 1, jeff_fs_blob_write_complete_cb, ctx);

#endif

    ctx->completed = true;
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


/* 
 *jeff_fs's posix api 
 */
/*read*/
static void jeff_fs_read_complete_cb(void *cb_arg, int bserrno){
    jeff_fs_context_t *ctx = (jeff_fs_context_t *)cb_arg;

    ctx->completed = true;
}

static void jeff_fs_read(void *arg){
    jeff_fs_context_t *ctx = (jeff_fs_context_t *)arg;
    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    ctx->read_buf = spdk_zmalloc(ctx->io_unit_size, 0x1000, NULL,
                                 SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
    if (!ctx->read_buf) {
        SPDK_NOTICELOG("Failed to allocate read buffer\n");
        jeff_fs_bs_unload(ctx);
        return;
    }

    spdk_blob_io_read(ctx->blb, ctx->channel, ctx->read_buf, 0, 1, jeff_fs_read_complete_cb, ctx);
}

static void jeff_fs_file_read(jeff_fs_context_t *ctx) {
    ctx->completed = false;
    poller(global_thread, jeff_fs_read, ctx, &ctx->completed);
}

/*write*/
static void jeff_fs_write_complete_cb(void *cb_arg, int bserrno){
    jeff_fs_context_t *ctx = (jeff_fs_context_t *)cb_arg;

    ctx->completed = true;
}

static void jeff_fs_write(void *arg) {
    jeff_fs_context_t *ctx = (jeff_fs_context_t *)arg;

    ctx->write_buf = spdk_zmalloc(ctx->io_unit_size, 0x1000, NULL,
                                  SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
    if (!ctx->write_buf) {
        jeff_fs_bs_unload(ctx);
        return;
    }
    memset(ctx->write_buf, '\0', ctx->io_unit_size);
    memset(ctx->write_buf, 'A', ctx->io_unit_size - 1);

    struct spdk_io_channel *channel = spdk_bs_alloc_io_channel(ctx->blb_store);
    if (!channel) {
        jeff_fs_bs_unload(ctx);
        return;
    }

    ctx->channel = channel;
    spdk_blob_io_write(ctx->blb, channel, ctx->write_buf, 0, 1, jeff_fs_write_complete_cb, ctx);

} 

static void jeff_fs_file_write(jeff_fs_context_t *ctx) {
    ctx->completed = false;
    poller(global_thread, jeff_fs_write, ctx, &ctx->completed);
}


/* load json config */
static const char *json_file = "/home/jeff/SPDK/hello_blob.json";

static void *jeff_fs_file_load(FILE *file, size_t *size) {
    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    uint8_t *newbuf, *buf = NULL;
	size_t rc, buf_size, cur_size = 0;

	*size = 0;
	buf_size = 128 * 1024;

	while (buf_size <= 1024 * 1024 * 1024) {
		newbuf = realloc(buf, buf_size);
		if (newbuf == NULL) {
			free(buf);
			return NULL;
		}
		buf = newbuf;

		rc = fread(buf + cur_size, 1, buf_size - cur_size, file);
		cur_size += rc;

		if (feof(file)) {
			*size = cur_size;
			return buf;
		}

		if (ferror(file)) {
			free(buf);
			return NULL;
		}

		buf_size *= 2;
	}

	free(buf);
	return NULL;
}

static void *jeff_fs_read_json_file(const char *path, size_t *size) {
    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    FILE *file = fopen(path, "rb");
    if (!file) {
        SPDK_ERRLOG("Failed to open JSON file: %s\n", path);
        return NULL;
    }

    void *data;

    data = jeff_fs_file_load(file, size);
	fclose(file);

	return data;   
}

static void jeff_fs_json_load_complete(int rc, void *ctx) {
    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    bool *completed = (bool *)ctx;
    *completed = true;
}

static void jeff_fs_json_load_fn(void *arg) {
    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    ssize_t json_size = 0;
    char *json = NULL;

    json = jeff_fs_read_json_file(json_file, &json_size);
    if (!json) {
        jeff_fs_json_load_complete(-EIO, arg);
        return;
    }

    spdk_subsystem_load_config(json, json_size, jeff_fs_json_load_complete, arg, true);   
    // spdk_subsystem_init_from_json_config(json_file, SPDK_DEFAULT_RPC_ADDR, jeff_fs_json_load_complete, arg, true); 

    free(json);
}

static void subsys_init_done(int rc, void *arg) {
    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    bool *completed = arg;
    *completed = true;
}

static void subsys_init_fn(void *arg) {
    SPDK_NOTICELOG("---> Enter %s\n", __func__);
    spdk_subsystem_init(subsys_init_done, arg);
}

int main(int argc, char *argv[]) {
#if 0
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
#else
    
    struct spdk_env_opts opts;
    spdk_env_opts_init(&opts);

    if (spdk_env_init(&opts) < 0) {
        SPDK_ERRLOG("spdk_app_init() failed\n");
        return -1;
    }

    spdk_log_set_print_level(SPDK_LOG_NOTICE);
    spdk_log_set_level(SPDK_LOG_NOTICE);
    spdk_log_open(NULL);

    spdk_thread_lib_init(NULL, 0);
    global_thread = spdk_thread_create("global", NULL);
    spdk_set_thread(global_thread);

    bool completed = false;
    poller(global_thread, jeff_fs_json_load_fn, &completed, &completed);

    bool inited = false;
    poller(global_thread, subsys_init_fn, &inited, &inited);

    jeff_fs_context_t *ctx = calloc(1, sizeof(jeff_fs_context_t));
    if (!ctx) {
        SPDK_NOTICELOG("Failed to allocate jeff_fs_context_t\n");
        return -1;
    }
    memset(ctx, 0, sizeof(jeff_fs_context_t));

    ctx->completed = false;
    poller(global_thread, jeff_fs_entry, ctx, &ctx->completed);

    jeff_fs_file_write(ctx);
    jeff_fs_file_read(ctx);

#endif
    return 0;
}
