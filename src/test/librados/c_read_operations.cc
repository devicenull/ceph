// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// Tests for the C API coverage of atomic read operations

#include <errno.h>
#include <string>

#include "include/rados/librados.h"
#include "test/librados/test.h"
#include "test/librados/TestCase.h"

const char *data = "testdata";
const char *obj = "testobj";
const int len = strlen(data);

class CReadOpsTest : public RadosTest {
protected:
  void write_object() {
    // Create an object and write to it
    ASSERT_EQ(len, rados_write(ioctx, obj, data, len, 0));
  }
  void remove_object() {
    ASSERT_EQ(0, rados_remove(ioctx, obj));
  }
  int cmp_xattr(const char *xattr, const char *value, size_t value_len,
		uint8_t cmp_op)
  {
    rados_read_op_t op = rados_create_read_op();
    rados_read_op_cmpxattr(op, xattr, cmp_op, value, value_len);
    int r = rados_read_op_operate(op, ioctx, obj, 0);
    rados_release_read_op(op);
    return r;
  }


  void compare_xattrs(char const* const* keys,
		      char const* const* vals,
		      const size_t *lens,
		      size_t len,
		      rados_xattrs_iter_t iter)
  {
    size_t i = 0;
    char *key = NULL;
    char *val = NULL;
    size_t val_len = 0;
    while (i < len) {
      ASSERT_EQ(0, rados_getxattrs_next(iter, (const char**) &key,
					(const char**) &val, &val_len));
      if (len == 0 && key == NULL && val == NULL)
	break;
      EXPECT_EQ(std::string(keys[i]), std::string(key));
      EXPECT_EQ(0, memcmp(vals[i], val, val_len));
      EXPECT_EQ(lens[i], val_len);
      ++i;
    }
    ASSERT_EQ(i, len);
    ASSERT_EQ(0, rados_getxattrs_next(iter, (const char**)&key,
				      (const char**)&val, &val_len));
    ASSERT_EQ(NULL, key);
    ASSERT_EQ(NULL, val);
    ASSERT_EQ(0u, val_len);
    rados_getxattrs_end(iter);
  }
};

TEST_F(CReadOpsTest, NewDelete) {
  rados_read_op_t op = rados_create_read_op();
  ASSERT_TRUE(op);
  rados_release_read_op(op);
}

TEST_F(CReadOpsTest, SetOpFlags) {
  write_object();

  rados_read_op_t op = rados_create_read_op();
  size_t bytes_read = 0;
  char *out = NULL;
  int rval = 0;
  rados_read_op_exec(op, "rbd", "get_id", NULL, 0, &out,
		     &bytes_read, &rval);
  rados_read_op_set_flags(op, LIBRADOS_OP_FLAG_FAILOK);
  EXPECT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
  EXPECT_EQ(0, rval);
  EXPECT_EQ(0u, bytes_read);
  EXPECT_EQ(NULL, out);
  rados_release_read_op(op);

  remove_object();
}

TEST_F(CReadOpsTest, AssertExists) {
  rados_read_op_t op = rados_create_read_op();
  rados_read_op_assert_exists(op);

  ASSERT_EQ(-ENOENT, rados_read_op_operate(op, ioctx, obj, 0));
  rados_release_read_op(op);

  op = rados_create_read_op();
  rados_read_op_assert_exists(op);

  rados_completion_t completion;
  ASSERT_EQ(0, rados_aio_create_completion(NULL, NULL, NULL, &completion));
  ASSERT_EQ(0, rados_aio_read_op_operate(op, ioctx, completion, obj, 0));
  rados_aio_wait_for_complete(completion);
  ASSERT_EQ(-ENOENT, rados_aio_get_return_value(completion));
  rados_release_read_op(op);

  write_object();

  op = rados_create_read_op();
  rados_read_op_assert_exists(op);
  ASSERT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
  rados_release_read_op(op);

  remove_object();
}

TEST_F(CReadOpsTest, CmpXattr) {
  write_object();

  char buf[len];
  memset(buf, 0xcc, sizeof(buf));

  const char *xattr = "test";
  rados_setxattr(ioctx, obj, xattr, buf, sizeof(buf));

  // equal value
  EXPECT_EQ(1, cmp_xattr(xattr, buf, sizeof(buf), LIBRADOS_CMPXATTR_OP_EQ));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, sizeof(buf), LIBRADOS_CMPXATTR_OP_NE));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, sizeof(buf), LIBRADOS_CMPXATTR_OP_GT));
  EXPECT_EQ(1, cmp_xattr(xattr, buf, sizeof(buf), LIBRADOS_CMPXATTR_OP_GTE));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, sizeof(buf), LIBRADOS_CMPXATTR_OP_LT));
  EXPECT_EQ(1, cmp_xattr(xattr, buf, sizeof(buf), LIBRADOS_CMPXATTR_OP_LTE));

  // < value
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, sizeof(buf) - 1, LIBRADOS_CMPXATTR_OP_EQ));
  EXPECT_EQ(1, cmp_xattr(xattr, buf, sizeof(buf) - 1, LIBRADOS_CMPXATTR_OP_NE));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, sizeof(buf) - 1, LIBRADOS_CMPXATTR_OP_GT));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, sizeof(buf) - 1, LIBRADOS_CMPXATTR_OP_GTE));
  EXPECT_EQ(1, cmp_xattr(xattr, buf, sizeof(buf) - 1, LIBRADOS_CMPXATTR_OP_LT));
  EXPECT_EQ(1, cmp_xattr(xattr, buf, sizeof(buf) - 1, LIBRADOS_CMPXATTR_OP_LTE));

  // > value
  memset(buf, 0xcd, sizeof(buf));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, sizeof(buf), LIBRADOS_CMPXATTR_OP_EQ));
  EXPECT_EQ(1, cmp_xattr(xattr, buf, sizeof(buf), LIBRADOS_CMPXATTR_OP_NE));
  EXPECT_EQ(1, cmp_xattr(xattr, buf, sizeof(buf), LIBRADOS_CMPXATTR_OP_GT));
  EXPECT_EQ(1, cmp_xattr(xattr, buf, sizeof(buf), LIBRADOS_CMPXATTR_OP_GTE));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, sizeof(buf), LIBRADOS_CMPXATTR_OP_LT));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, sizeof(buf), LIBRADOS_CMPXATTR_OP_LTE));

  // it compares C strings, so NUL at the beginning is the same
  // regardless of the following data
  rados_setxattr(ioctx, obj, xattr, "\0\0", 1);
  buf[0] = '\0';
  EXPECT_EQ(1, cmp_xattr(xattr, buf, 2, LIBRADOS_CMPXATTR_OP_EQ));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, 2, LIBRADOS_CMPXATTR_OP_NE));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, 2, LIBRADOS_CMPXATTR_OP_GT));
  EXPECT_EQ(1, cmp_xattr(xattr, buf, 2, LIBRADOS_CMPXATTR_OP_GTE));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, 2, LIBRADOS_CMPXATTR_OP_LT));
  EXPECT_EQ(1, cmp_xattr(xattr, buf, 2, LIBRADOS_CMPXATTR_OP_LTE));

  buf[1] = '\0';
  EXPECT_EQ(1, cmp_xattr(xattr, buf, 2, LIBRADOS_CMPXATTR_OP_EQ));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, 2, LIBRADOS_CMPXATTR_OP_NE));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, 2, LIBRADOS_CMPXATTR_OP_GT));
  EXPECT_EQ(1, cmp_xattr(xattr, buf, 2, LIBRADOS_CMPXATTR_OP_GTE));
  EXPECT_EQ(-ECANCELED, cmp_xattr(xattr, buf, 2, LIBRADOS_CMPXATTR_OP_LT));
  EXPECT_EQ(1, cmp_xattr(xattr, buf, 2, LIBRADOS_CMPXATTR_OP_LTE));

  remove_object();
}

TEST_F(CReadOpsTest, Read) {
  write_object();

  char buf[len];
  // check that using read_ops returns the same data with
  // or without bytes_read and rval out params
  {
    rados_read_op_t op = rados_create_read_op();
    rados_read_op_read(op, 0, len, buf, NULL, NULL);
    ASSERT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
    ASSERT_EQ(0, memcmp(data, buf, len));
    rados_release_read_op(op);
  }

  {
    rados_read_op_t op = rados_create_read_op();
    int rval;
    rados_read_op_read(op, 0, len, buf, NULL, &rval);
    ASSERT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
    ASSERT_EQ(0, rval);
    ASSERT_EQ(0, memcmp(data, buf, len));
    rados_release_read_op(op);
  }

  {
    rados_read_op_t op = rados_create_read_op();
    size_t bytes_read = 0;
    rados_read_op_read(op, 0, len, buf, &bytes_read, NULL);
    ASSERT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
    ASSERT_EQ(len, (int)bytes_read);
    ASSERT_EQ(0, memcmp(data, buf, len));
    rados_release_read_op(op);
  }

  {
    rados_read_op_t op = rados_create_read_op();
    size_t bytes_read = 0;
    int rval;
    rados_read_op_read(op, 0, len, buf, &bytes_read, &rval);
    ASSERT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
    ASSERT_EQ(len, (int)bytes_read);
    ASSERT_EQ(0, rval);
    ASSERT_EQ(0, memcmp(data, buf, len));
    rados_release_read_op(op);
  }

  remove_object();
}

TEST_F(CReadOpsTest, ShortRead) {
  write_object();

  char buf[len * 2];
  // check that using read_ops returns the same data with
  // or without bytes_read and rval out params
  {
    rados_read_op_t op = rados_create_read_op();
    rados_read_op_read(op, 0, len * 2, buf, NULL, NULL);
    ASSERT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
    ASSERT_EQ(0, memcmp(data, buf, len));
    rados_release_read_op(op);
  }

  {
    rados_read_op_t op = rados_create_read_op();
    int rval;
    rados_read_op_read(op, 0, len * 2, buf, NULL, &rval);
    ASSERT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
    ASSERT_EQ(0, rval);
    ASSERT_EQ(0, memcmp(data, buf, len));
    rados_release_read_op(op);
  }

  {
    rados_read_op_t op = rados_create_read_op();
    size_t bytes_read = 0;
    rados_read_op_read(op, 0, len * 2, buf, &bytes_read, NULL);
    ASSERT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
    ASSERT_EQ(len, (int)bytes_read);
    ASSERT_EQ(0, memcmp(data, buf, len));
    rados_release_read_op(op);
  }

  {
    rados_read_op_t op = rados_create_read_op();
    size_t bytes_read = 0;
    int rval;
    rados_read_op_read(op, 0, len * 2, buf, &bytes_read, &rval);
    ASSERT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
    ASSERT_EQ(len, (int)bytes_read);
    ASSERT_EQ(0, rval);
    ASSERT_EQ(0, memcmp(data, buf, len));
    rados_release_read_op(op);
  }

  remove_object();
}

TEST_F(CReadOpsTest, Exec) {
  // create object so we don't get -ENOENT
  write_object();

  rados_read_op_t op = rados_create_read_op();
  ASSERT_TRUE(op);
  size_t bytes_read = 0;
  char *out = NULL;
  int rval = 0;
  rados_read_op_exec(op, "rbd", "get_all_features", NULL, 0, &out,
		     &bytes_read, &rval);
  ASSERT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
  rados_release_read_op(op);
  EXPECT_EQ(0, rval);
  EXPECT_TRUE(out);
  uint64_t features;
  EXPECT_EQ(sizeof(features), bytes_read);
  // make sure buffer is at least as long as it claims
  ASSERT_TRUE(out[bytes_read-1] == out[bytes_read-1]);
  rados_buffer_free(out);

  remove_object();
}

TEST_F(CReadOpsTest, ExecUserBuf) {
  // create object so we don't get -ENOENT
  write_object();

  rados_read_op_t op = rados_create_read_op();
  size_t bytes_read = 0;
  uint64_t features;
  char out[sizeof(features)];
  int rval = 0;
  rados_read_op_exec_user_buf(op, "rbd", "get_all_features", NULL, 0, out,
			      sizeof(out), &bytes_read, &rval);
  ASSERT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
  rados_release_read_op(op);
  EXPECT_EQ(0, rval);
  EXPECT_EQ(sizeof(features), bytes_read);

  // buffer too short
  bytes_read = 1024;
  op = rados_create_read_op();
  rados_read_op_exec_user_buf(op, "rbd", "get_all_features", NULL, 0, out,
			      sizeof(features) - 1, &bytes_read, &rval);
  ASSERT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
  EXPECT_EQ(0u, bytes_read);
  EXPECT_EQ(-ERANGE, rval);

  // input buffer and no rval or bytes_read
  op = rados_create_read_op();
  rados_read_op_exec_user_buf(op, "rbd", "get_all_features", out, sizeof(out),
			      out, sizeof(out), NULL, NULL);
  ASSERT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
  rados_release_read_op(op);

  remove_object();
}

TEST_F(CReadOpsTest, Stat) {
  rados_read_op_t op = rados_create_read_op();
  uint64_t size = 1;
  int rval;
  rados_read_op_stat(op, &size, NULL, &rval);
  EXPECT_EQ(-ENOENT, rados_read_op_operate(op, ioctx, obj, 0));
  EXPECT_EQ(-EIO, rval);
  EXPECT_EQ(1u, size);
  rados_release_read_op(op);

  write_object();

  op = rados_create_read_op();
  rados_read_op_stat(op, &size, NULL, &rval);
  EXPECT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
  EXPECT_EQ(0, rval);
  EXPECT_EQ(len, (int)size);
  rados_release_read_op(op);

  op = rados_create_read_op();
  rados_read_op_stat(op, NULL, NULL, NULL);
  EXPECT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
  rados_release_read_op(op);

  remove_object();

  op = rados_create_read_op();
  rados_read_op_stat(op, NULL, NULL, NULL);
  EXPECT_EQ(-ENOENT, rados_read_op_operate(op, ioctx, obj, 0));
  rados_release_read_op(op);
}

TEST_F(CReadOpsTest, GetXattrs) {
  write_object();

  char *keys[] = {(char*)"bar",
		  (char*)"foo",
		  (char*)"test1",
		  (char*)"test2"};
  char *vals[] = {(char*)"",
		  (char*)"\0",
		  (char*)"abc",
		  (char*)"va\0lue"};
  size_t lens[] = {0, 1, 3, 6};

  int rval = 1;
  rados_read_op_t op = rados_create_read_op();
  rados_xattrs_iter_t it;
  rados_read_op_getxattrs(op, &it, &rval);
  EXPECT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
  EXPECT_EQ(0, rval);
  rados_release_read_op(op);
  compare_xattrs(keys, vals, lens, 0, it);

  for (int i = 0; i < 4; ++i)
    rados_setxattr(ioctx, obj, keys[i], vals[i], lens[i]);

  rval = 1;
  op = rados_create_read_op();
  rados_read_op_getxattrs(op, &it, &rval);
  EXPECT_EQ(0, rados_read_op_operate(op, ioctx, obj, 0));
  EXPECT_EQ(0, rval);
  rados_release_read_op(op);
  compare_xattrs(keys, vals, lens, 4, it);

  remove_object();
}
