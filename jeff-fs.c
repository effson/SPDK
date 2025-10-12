#include <stdio.h>
#include <spdk/event.h>
#include <spdk/blob_bdev.h>
#include <spdk/blob.h>

//typedef void (*spdk_bdev_event_cb_t)(enum spdk_bdev_event_type type, struct spdk_bdev *bdev,
//               void *event_ctx);

static void jeff_fs_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev,
               void *event_ctx) {
    // printf("jeff-fs bdev event callback\n");
}

static void jeff_fs_create_complete_cb(void *cb_arg, spdk_blob_id blobid, int bserrno){

}

static void jeff_fs_init_complete_cb(void *cb_arg, struct spdk_blob_store *bs,
		int bserrno) {

    /*typedef void (*spdk_blob_op_with_id_complete)(void *cb_arg, spdk_blob_id blobid, int bserrno);*/
    spdk_bs_create_blob(bs, jeff_fs_create_complete_cb, NULL);
}

/*
int spdk_bdev_create_bs_dev_ext(const char *bdev_name, spdk_bdev_event_cb_t event_cb,
			    void *event_ctx, struct spdk_bs_dev **bs_dev)
{
	return spdk_bdev_create_bs_dev(bdev_name, true, NULL, 0, event_cb, event_ctx, bs_dev);
}*/

static void jeff_fs_entry (void *ctx) {
    // printf("jeff-fs started\n");

    const char *bdev_name = "Malloc0";
    struct spdk_bs_dev *bs_dev = NULL;
    int rc = spdk_bdev_create_bs_dev_ext(bdev_name, jeff_fs_bdev_event_cb, NULL, &bs_dev);
    if (rc != 0) {
        fprintf(stderr, "Failed to create blobstore on bdev %s\n", bdev_name);
        spdk_app_stop(-1);
        return;
    }

    /*void
      spdk_bs_init(struct spdk_bs_dev *dev, struct spdk_bs_opts *o,
	        spdk_bs_op_with_handle_complete cb_fn, void *cb_arg)

      typedef void (*spdk_bs_op_with_handle_complete)(void *cb_arg, struct spdk_blob_store *bs,
		int bserrno);
    */

    spdk_bs_init(bs_dev, NULL, jeff_fs_init_complete_cb, NULL);
}

int main(int argc, char *argv[]) {

    struct spdk_app_opts opts = {};
    spdk_app_opts_init(&opts, sizeof(struct spdk_app_opts));
    opts.name = "jeff-fs";
    opts.json_config_file = argv[1];

    spdk_app_start(&opts, jeff_fs_entry, NULL);

    return 0;
}
