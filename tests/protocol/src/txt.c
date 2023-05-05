/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <math.h>

#include "../../src/thingset_internal.h"

#include "data.h"
#include "test_utils.h"

static struct thingset_context ts;

ZTEST(thingset_txt, test_response)
{
    char act[100];
    int len;

    char rsp_err[] = ":A0 \"test message 1\"";
    char rsp_changed[] = ":84";

    ts.rsp = act;
    ts.rsp_size = sizeof(act);

    len = thingset_txt_serialize_response(&ts, 0xA0, "test message %d", 1);
    zassert_equal(len, strlen(rsp_err));
    zassert_mem_equal(ts.rsp, rsp_err, strlen(ts.rsp), "act: %s\nexp: %s", ts.rsp, rsp_err);

    len = thingset_txt_serialize_response(&ts, 0x84, NULL);
    zassert_equal(len, strlen(rsp_changed));
    zassert_mem_equal(ts.rsp, rsp_changed, strlen(ts.rsp), "act: %s\nexp: %s", ts.rsp, rsp_changed);
}

ZTEST(thingset_txt, test_get_root)
{
    const char req[] = "?";
    const char rsp_exp[] =
        ":85 {"
        "\"t_s\":1000,"
        "\"cNodeID\":\"ABCD1234\","
        "\"Types\":null,"
        "\"Arrays\":null,"
        "\"Exec\":null,"
        "\"Access\":null,"
        "\"Records\":2,"
        "\"Nested\":null,"
        "\"mLive\":[\"t_s\",\"Types/wBool\",\"Nested/rBeginning\",\"Nested/Obj2/rItem2_V\"]"
        "}";

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_get_nested)
{
    const char req[] = "?Nested";
    const char rsp_exp[] =
        ":85 {"
        "\"rBeginning\":1,"
        "\"Obj1\":null,"
        "\"rBetween\":2,"
        "\"Obj2\":null,"
        "\"rEnd\":3"
        "}";

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_get_single_value)
{
    const char req[] = "?Nested/Obj1/rItem2_V";
    const char rsp_exp[] = ":85 1.2";

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_get_exec)
{
    const char req[] = "?Exec";
    const char rsp_exp[] =
        ":85 {"
        "\"xVoid\":[],"
        "\"xVoidParams\":[\"lBool\"],"
        "\"xI32Params\":[\"uString\",\"nNumber\"]"
        "}";

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_fetch_root_names)
{
    const char req[] = "? null";
    const char rsp_exp[] =
        ":85 ["
        "\"t_s\","
        "\"cNodeID\","
        "\"Types\","
        "\"Arrays\","
        "\"Exec\","
        "\"Access\","
        "\"Records\","
        "\"Nested\","
        "\"mLive\""
        "]";

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_fetch_nested_names)
{
    const char req[] = "?Nested null";
    const char rsp_exp[] =
        ":85 ["
        "\"rBeginning\","
        "\"Obj1\","
        "\"rBetween\","
        "\"Obj2\","
        "\"rEnd\""
        "]";

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_fetch_node_id)
{
    const char req[] = "? [\"cNodeID\"]";
    const char rsp_exp[] = ":85 [\"ABCD1234\"]";

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_fetch_bad_elem)
{
    const char req[] = "? [true]";
    const char rsp_exp[] = ":A0 \"Only string elements allowed\"";

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_fetch_not_found)
{
    const char req[] = "? [\"foo\"]";
    const char rsp_exp[] = ":A4 \"Item foo not found\"";

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_fetch_group)
{
    const char req[] = "? [\"Nested\"]";
    const char rsp_exp[] = ":A0 \"Nested is a group\"";

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_fetch_multiple)
{
    THINGSET_ASSERT_REQUEST_TXT("?Types [\"wF32\",\"wBool\",\"wU32\"]", ":85 [-3.2,true,32]");
}

ZTEST(thingset_txt, test_fetch_rounded)
{
    float bak = f32;
    f32 = 3.15;

    THINGSET_ASSERT_REQUEST_TXT("?Types [\"wF32\"]", ":85 [3.2]");

    f32 = bak;
}

ZTEST(thingset_txt, test_fetch_nan)
{
    float bak = f32;
    uint32_t nan = 0x7F800001;
    f32 = *(float *)&nan;

    zassert_true(isnan(f32));
    THINGSET_ASSERT_REQUEST_TXT("?Types [\"wF32\"]", ":85 [null]");

    f32 = bak;
}

ZTEST(thingset_txt, test_fetch_inf)
{
    float bak = f32;
    uint32_t inf = 0x7F800000;
    f32 = *(float *)&inf;

    THINGSET_ASSERT_REQUEST_TXT("?Types [\"wF32\"]", ":85 [null]");

    f32 = bak;
}

ZTEST(thingset_txt, test_fetch_int32_array)
{
    THINGSET_ASSERT_REQUEST_TXT("?Arrays [\"wI32\"]", ":85 [[-1,-2,-3]]");
}

ZTEST(thingset_txt, test_fetch_float_array)
{
    THINGSET_ASSERT_REQUEST_TXT("?Arrays [\"wF32\"]", ":85 [[-1.1,-2.2,-3.3]]");
}

ZTEST(thingset_txt, test_fetch_num_records)
{
    THINGSET_ASSERT_REQUEST_TXT("?Records", ":85 2");
}

ZTEST(thingset_txt, test_fetch_record)
{
    const char req[] = "?Records/1";
    const char rsp_exp[] =
        ":85 {"
        "\"t_s\":2,"
        "\"wBool\":true,"
        "\"wU8\":8,\"wI8\":-8,"
        "\"wU16\":16,\"wI16\":-16,"
        "\"wU32\":32,\"wI32\":-32,"
        "\"wU64\":64,\"wI64\":-64,"
        "\"wF32\":-3.2,\"wDecFrac\":-32e-2,"
        "\"wString\":\"string\""
        "}";

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_update_timestamp_zero)
{
    THINGSET_ASSERT_REQUEST_TXT("= {\"t_s\":0}", ":84");
    zassert_equal(timestamp, 0);

    timestamp = 1000;
}

ZTEST(thingset_txt, test_update_wrong_data_structure)
{
    THINGSET_ASSERT_REQUEST_TXT("=Types [\"wF32\":54.3", ":A0 \"JSON parsing error\"");
    THINGSET_ASSERT_REQUEST_TXT("=Types{\"wF32\":54.3}", ":A4 \"Invalid endpoint\"");
}

ZTEST(thingset_txt, test_update_whitespaces)
{
    THINGSET_ASSERT_REQUEST_TXT("=Types {    \"wF32\" : 52.8,\"wI32\":50.6}", ":84");

    zassert_equal((float)52.8, f32);
    zassert_equal(50, i32);

    f32 = 3.2F;
    i32 = -32;
}

#if CONFIG_THINGSET_BYTES_TYPE_SUPPORT

ZTEST(thingset_txt, test_update_bytes_buffer)
{
    THINGSET_ASSERT_REQUEST_TXT("=Types {\"wBytes\":\"QUJDREVGRw==\"}", ":84");

    zassert_equal(7, bytes_item.num_bytes);
}

#else

ZTEST(thingset_txt, test_update_bytes_buffer)
{
    THINGSET_ASSERT_REQUEST_TXT("=Types {\"wBytes\":\"QUJDREVGRw==\"}", ":AF");
}

#endif

ZTEST(thingset_txt, test_update_readonly)
{
    THINGSET_ASSERT_REQUEST_TXT("=Access {\"rItem\" : 52}", ":A3 \"Item rItem is read-only\"");
}

ZTEST(thingset_txt, test_update_wrong_path)
{
    THINGSET_ASSERT_REQUEST_TXT("=Type {\"wI32\" : 52}", ":A4 \"Invalid endpoint\"");
}

ZTEST(thingset_txt, test_update_unknown_object)
{
    THINGSET_ASSERT_REQUEST_TXT("=Types {\"wI3\" : 52}", ":A4 \"Item wI3 not found\"");
}

ZTEST(thingset_txt, test_exec)
{
    const char req[] = "!";       /* invalid request */
    const char rsp_exp[] = ":C1"; /* not yet implemented */

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_create)
{
    const char req[] = "+";       /* invalid request */
    const char rsp_exp[] = ":C1"; /* not yet implemented */

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_delete)
{
    const char req[] = "-";       /* invalid request */
    const char rsp_exp[] = ":C1"; /* not yet implemented */

    THINGSET_ASSERT_REQUEST_TXT(req, rsp_exp);
}

ZTEST(thingset_txt, test_desire_timestamp_zero)
{
    const char des[] = "@t_s 0";
    const int err_exp = -THINGSET_ERR_NOT_IMPLEMENTED;

    THINGSET_ASSERT_DESIRE_TXT(des, err_exp);
}

static void *thingset_setup(void)
{
    thingset_init_global(&ts);

    return NULL;
}

ZTEST_SUITE(thingset_txt, NULL, thingset_setup, NULL, NULL, NULL);
