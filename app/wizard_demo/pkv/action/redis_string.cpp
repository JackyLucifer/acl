//
// Created by shuxin ����zheng on 2023/7/31.
//

#include "stdafx.h"
#include "proto/redis_coder.h"
#include "proto/redis_object.h"
#include "dao/db.h"

#include "redis_handler.h"
#include "redis_string.h"

namespace pkv {

redis_string::redis_string(redis_handler& handler, const redis_object &obj)
: handler_(handler), obj_(obj)
{}

bool redis_string::set(redis_coder& result) {
    auto& objs = obj_.get_objects();
    if (objs.size() < 3) {
        logger_error("invalid SET params' size=%zd", objs.size());
        return false;
    }

    auto key = objs[1]->get_str();
    if (key == nullptr || *key == 0) {
        logger_error("key null");
        return false;
    }

    auto value = objs[2]->get_str();
    if (value == nullptr || *value == 0) {
        logger_error("value null");
        return false;
    }

    if (!var_cfg_disable_serialize) {
        std::string buff;
        auto& coder = handler_.get_coder();
        coder.clear();

        coder.create_object().create_child().set_string("string", true) // type
            .create_child().set_number(-1);    // expire time
        coder.create_object().set_string(value);
        coder.to_string(buff);

        //printf("buf=[%s]\n", buff.c_str());

        if (!var_cfg_disable_save) {
            if (!handler_.get_db()->set(key, buff.c_str())) {
                logger_error("db set error, key=%s", key);
                return false;
            }
        }
    }

    result.create_object().set_status("OK");
    return true;
}

bool redis_string::get(redis_coder& result) {
    auto& objs = obj_.get_objects();
    if (objs.size() < 2) {
        logger_error("invalid GET params' size=%zd", objs.size());
        return false;
    }

    auto key = objs[1]->get_str();
    if (key == nullptr || *key == 0) {
        logger_error("key null");
        return false;
    }

    std::string buff;
    if (!handler_.get_db()->get(key, buff) || buff.empty()) {
        logger_error("db get error, key=%s", key);
        return false;
    }

    //printf(">>>get key=%s, val=[%s]\n", key, buff.c_str());

    auto& coder = handler_.get_coder();
    coder.clear();

    size_t len = buff.size();
    (void) coder.update(buff.c_str(), len);
    if (len > 0) {
        logger_error("invalid buff in db, key=%s", key);
        return false;
    }

    auto& objs2 = coder.get_objects();
    if (objs2.size() != 2) {
        logger_error("invalid object in db, key=%s, size=%zd", key, objs2.size());
        return false;
    }

    auto o = objs2[1];
    if (o->get_type() != REDIS_OBJ_STRING) {
        logger_error("invalid object type=%d, key=%s", (int) o->get_type(), key);
        return false;
    }

    auto v = o->get_str();
    if (v == nullptr || *v == 0) {
        logger_error("value null, key=%s", key);
        return false;
    }

    result.create_object().set_string(v);
    return true;
}

} // namespace pkv
