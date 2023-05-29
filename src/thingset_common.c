/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "thingset/thingset.h"
#include "thingset_internal.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int thingset_common_serialize_group(struct thingset_context *ts,
                                    const struct thingset_data_object *object)
{
    int err;

    err = ts->api->serialize_map_start(ts);
    if (err != 0) {
        return err;
    }

    if (object->data.group_callback != NULL) {
        object->data.group_callback(THINGSET_CALLBACK_PRE_READ);
    }

    for (unsigned int i = 0; i < ts->num_objects; i++) {
        if (ts->data_objects[i].parent_id == object->id
            && (ts->data_objects[i].access & THINGSET_READ_MASK))
        {
            err = ts->api->serialize_key_value(ts, &ts->data_objects[i]);
            if (err != 0) {
                return err;
            }
        }
    }

    if (object->data.group_callback != NULL) {
        object->data.group_callback(THINGSET_CALLBACK_POST_READ);
    }

    return ts->api->serialize_map_end(ts);
}

int thingset_common_serialize_record(struct thingset_context *ts,
                                     const struct thingset_data_object *object, int record_index)
{
    struct thingset_records *records = object->data.records;
    int err;

    err = ts->api->serialize_map_start(ts);
    if (err != 0) {
        return err;
    }

    /* record item definitions are expected to start behind record data object */
    const struct thingset_data_object *item = object + 1;
    while (item < &ts->data_objects[ts->num_objects] && item->parent_id == object->id) {
        /* create new object with data pointer including offset */
        uint8_t *data_ptr =
            (uint8_t *)records->records + record_index * records->record_size + item->data.offset;
        struct thingset_data_object obj = {
            item->parent_id, item->id, item->name, { .u8 = data_ptr }, item->type, item->detail,
        };

        err = ts->api->serialize_key_value(ts, &obj);
        if (err != 0) {
            return err;
        }

        item++;
    }

    return ts->api->serialize_map_end(ts);
}

int thingset_common_get(struct thingset_context *ts)
{
    int err;

    ts->api->serialize_response(ts, THINGSET_STATUS_CONTENT, NULL);

    switch (ts->endpoint.object->type) {
        case THINGSET_TYPE_GROUP:
            err = thingset_common_serialize_group(ts, ts->endpoint.object);
            break;
        case THINGSET_TYPE_FN_VOID:
        case THINGSET_TYPE_FN_I32:
            /* bad request, as we can't read exec object's values */
            err = -THINGSET_ERR_BAD_REQUEST;
            break;
        case THINGSET_TYPE_RECORDS:
            if (ts->endpoint.index != INDEX_NONE) {
                err = thingset_common_serialize_record(ts, ts->endpoint.object, ts->endpoint.index);
                break;
            }
            /* fallthrough */
        default:
            err = ts->api->serialize_value(ts, ts->endpoint.object);
            break;
    }

    if (err == 0) {
        return ts->rsp_pos;
    }
    else {
        return ts->api->serialize_response(ts, -err, NULL);
    }
}

int thingset_common_fetch(struct thingset_context *ts)
{
    int err;

    /* initialize response with success message */
    ts->api->serialize_response(ts, THINGSET_STATUS_CONTENT, NULL);

    ts->api->serialize_list_start(ts);

    if (ts->api->deserialize_null(ts) == 0) {
        /* fetch names */
        for (unsigned int i = 0; i < ts->num_objects; i++) {
            if ((ts->data_objects[i].access & THINGSET_READ_MASK)
                && (ts->data_objects[i].parent_id == ts->endpoint.object->id))
            {
                err = ts->api->serialize_key(ts, &ts->data_objects[i]);
                if (err != 0) {
                    return ts->api->serialize_response(ts, -err, NULL);
                }
            }
        }
    }
    else if (ts->api->deserialize_list_start(ts) == 0) {
        /* fetch values */
        if (ts->endpoint.object->data.group_callback != NULL) {
            ts->endpoint.object->data.group_callback(THINGSET_CALLBACK_PRE_READ);
        }

        const struct thingset_data_object *object;
        while ((err = ts->api->deserialize_child(ts, ts->endpoint.object->id, &object))
               != -THINGSET_ERR_DESERIALIZATION_FINISHED)
        {
            if (err != 0) {
                return ts->api->serialize_response(ts, -err, NULL);
            }

            if (object->type == THINGSET_TYPE_GROUP) {
                return ts->api->serialize_response(ts, THINGSET_ERR_BAD_REQUEST, "%s is a group",
                                                   object->name);
            }

            if ((object->access & THINGSET_READ_MASK & ts->auth_flags) == 0) {
                if (object->access & THINGSET_READ_MASK) {
                    return ts->api->serialize_response(ts, THINGSET_ERR_UNAUTHORIZED,
                                                       "Authentication required for %s",
                                                       object->name);
                }
                else {
                    return ts->api->serialize_response(ts, THINGSET_ERR_FORBIDDEN,
                                                       "Reading %s forbidden", object->name);
                }
            }

            err = ts->api->serialize_value(ts, object);
            if (err != 0) {
                return ts->api->serialize_response(ts, -err, NULL);
            }
        }

        if (ts->endpoint.object->data.group_callback != NULL) {
            ts->endpoint.object->data.group_callback(THINGSET_CALLBACK_POST_READ);
        }
    }
    else {
        return ts->api->serialize_response(ts, THINGSET_ERR_BAD_REQUEST, "Invalid payload");
    }

    ts->api->serialize_list_end(ts);

    return 0;
}

int thingset_common_exec(struct thingset_context *ts)
{
    int err;

    err = ts->api->deserialize_list_start(ts);
    if (err != 0) {
        err = ts->api->deserialize_finish(ts);
        if (err != 0) {
            return ts->api->serialize_response(ts, THINGSET_ERR_BAD_REQUEST, "Invalid parameters");
        }
    }

    if ((ts->endpoint.object->access & THINGSET_WRITE_MASK)
        && (ts->endpoint.object->type == THINGSET_TYPE_FN_VOID
            || ts->endpoint.object->type == THINGSET_TYPE_FN_I32))
    {
        /* object is generally executable, but are we authorized? */
        if ((ts->endpoint.object->access & THINGSET_WRITE_MASK & ts->auth_flags) == 0) {
            return ts->api->serialize_response(ts, THINGSET_ERR_UNAUTHORIZED,
                                               "Authentication required");
        }
    }
    else {
        return ts->api->serialize_response(ts, THINGSET_ERR_FORBIDDEN, "%s is not executable",
                                           ts->endpoint.object->name);
    }

    for (unsigned int i = 0; i < ts->num_objects; i++) {
        if (ts->data_objects[i].parent_id == ts->endpoint.object->id) {
            err = ts->api->deserialize_value(ts, &ts->data_objects[i]);
            if (err == -THINGSET_ERR_DESERIALIZATION_FINISHED) {
                /* more child objects found than parameters were passed */
                return ts->api->serialize_response(ts, THINGSET_ERR_BAD_REQUEST,
                                                   "Not enough parameters");
            }
            else if (err != 0) {
                /* deserializing the value was not successful */
                return ts->api->serialize_response(ts, -err, NULL);
            }
        }
    }

    err = ts->api->deserialize_finish(ts);
    if (err != 0) {
        /* more parameters passed than child objects found */
        return ts->api->serialize_response(ts, THINGSET_ERR_BAD_REQUEST, "Too many parameters");
    }

    ts->api->serialize_response(ts, THINGSET_STATUS_CHANGED, NULL);

    /* if we got here, finally create function pointer and call function */
    if (ts->endpoint.object->type == THINGSET_TYPE_FN_I32) {
        int32_t ret = ts->endpoint.object->data.i32_fn();
        struct thingset_data_object ret_object = THINGSET_ITEM_INT32(0, 0, "", &ret, 0, 0);
        err = ts->api->serialize_value(ts, &ret_object);
        if (err != 0) {
            return ts->api->serialize_response(ts, THINGSET_ERR_RESPONSE_TOO_LARGE, NULL);
        }
    }
    else {
        ts->endpoint.object->data.void_fn();
    }

    return 0;
}

int thingset_common_create_delete(struct thingset_context *ts, bool create)
{
    int err;

    if (ts->endpoint.object->id == 0) {
        return ts->api->serialize_response(ts, THINGSET_ERR_BAD_REQUEST, "Endpoint item required");
    }

    if (ts->endpoint.object->type == THINGSET_TYPE_ARRAY) {
        return ts->api->serialize_response(ts, THINGSET_ERR_NOT_IMPLEMENTED,
                                           "Arrays not yet supported");
    }
    else if (ts->endpoint.object->type == THINGSET_TYPE_SUBSET) {
#if CONFIG_THINGSET_IMMUTABLE_OBJECTS
        return ts->api->serialize_response(ts, THINGSET_ERR_METHOD_NOT_ALLOWED,
                                           "Subset is immutable");
#else
        const char *str_start;
        size_t str_len;
        err = ts->api->deserialize_string(ts, &str_start, &str_len);
        if (err != 0) {
            return ts->api->serialize_response(ts, THINGSET_ERR_UNSUPPORTED_FORMAT, NULL);
        }

        struct thingset_endpoint element;
        int ret = thingset_endpoint_by_path(ts, &element, str_start, str_len);
        if (ret >= 0 && element.index == INDEX_NONE) {
            if (create) {
                element.object->subsets |= ts->endpoint.object->data.subset;
                return ts->api->serialize_response(ts, THINGSET_STATUS_CREATED, NULL);
            }
            else {
                element.object->subsets &= ~ts->endpoint.object->data.subset;
                return ts->api->serialize_response(ts, THINGSET_STATUS_DELETED, NULL);
            }
        }
        return ts->api->serialize_response(ts, THINGSET_ERR_NOT_FOUND, NULL);
#endif /* CONFIG_THINGSET_IMMUTABLE_OBJECTS */
    }

    return ts->api->serialize_response(ts, THINGSET_ERR_METHOD_NOT_ALLOWED, NULL);
}

int thingset_common_create(struct thingset_context *ts)
{
    return thingset_common_create_delete(ts, true);
}

int thingset_common_delete(struct thingset_context *ts)
{
    return thingset_common_create_delete(ts, false);
}
