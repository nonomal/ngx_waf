#ifndef __NGX_HTTP_WAF_MODULE_UNDER_ATTACK_H__
#define __NGX_HTTP_WAF_MODULE_UNDER_ATTACK_H__


#include <ngx_http_waf_module_type.h>
#include <ngx_http_waf_module_util.h>

extern ngx_module_t ngx_http_waf_module; /**< 模块详情 */


static ngx_int_t ngx_http_waf_check_under_attack(ngx_http_request_t* r, ngx_int_t* out_http_status);


static ngx_int_t ngx_http_waf_gen_cookie(ngx_http_request_t *r);


static ngx_int_t ngx_http_waf_gen_verification(ngx_http_request_t *r, 
                                                u_char* uid, 
                                                size_t uid_len, 
                                                u_char* dst, 
                                                size_t dst_len, 
                                                u_char* now,
                                                size_t now_len);


static void ngx_http_waf_gen_ctx_and_header_location(ngx_http_request_t *r);


static ngx_int_t ngx_http_waf_check_under_attack(ngx_http_request_t* r, ngx_int_t* out_http_status) {
    ngx_http_waf_srv_conf_t* srv_conf = ngx_http_get_module_srv_conf(r, ngx_http_waf_module);

    if (srv_conf->waf_under_attack == 0 || srv_conf->waf_under_attack == NGX_CONF_UNSET) {
        return NGX_HTTP_WAF_NOT_MATCHED;
    }

    if (ngx_strncmp(r->uri.data, 
                    srv_conf->waf_under_attack_uri.data, 
                    ngx_max(r->uri.len, srv_conf->waf_under_attack_uri.len)) == 0) {
        return NGX_HTTP_WAF_NOT_MATCHED;
    }

    ngx_table_elt_t **ppcookie = (ngx_table_elt_t **)(r->headers_in.cookies.elts);
    ngx_str_t __waf_under_attack_time = { 0, NULL };
    ngx_str_t __waf_under_attack_uid = { 0, NULL };
    ngx_str_t __waf_under_attack_verification = { 0, NULL };

    

    for (size_t i = 0; i < r->headers_in.cookies.nelts; i++, ppcookie++) {
        ngx_table_elt_t *native_cookie = *ppcookie;
        UT_array* cookies = NULL;
        ngx_str_split(&(native_cookie->value), ';', native_cookie->value.len, &cookies);
        ngx_str_t* p = NULL;
        
        while (p = (ngx_str_t*)utarray_next(cookies, p), p != NULL) {
            UT_array* key_and_value = NULL;

            ngx_str_t temp;
            temp.data = p->data;
            temp.len = p->len;
            if (p->data[0] == ' ') {
                temp.data += 1;
                temp.len -= 1;
            }

            ngx_str_split(&temp, '=', temp.len, &key_and_value);
            if (utarray_len(key_and_value) == 2) {
                ngx_str_t* key = NULL;
                ngx_str_t* value = NULL;
                key = (ngx_str_t*)utarray_next(key_and_value, NULL);
                value = (ngx_str_t*)utarray_next(key_and_value, key);

                if (ngx_strcmp(key->data, "__waf_under_attack_time") == 0) {
                    __waf_under_attack_time.data = ngx_pnalloc(r->pool, sizeof(u_char) * (NGX_HTTP_WAF_UNDER_ATTACH_TIME_LEN + 1));
                    ngx_memzero(__waf_under_attack_time.data, sizeof(u_char) * (NGX_HTTP_WAF_UNDER_ATTACH_TIME_LEN + 1));
                    ngx_memcpy(__waf_under_attack_time.data, value->data, 
                              sizeof(u_char) * ngx_min(value->len, NGX_HTTP_WAF_UNDER_ATTACH_TIME_LEN));
                    __waf_under_attack_time.len = value->len;
                } else if (ngx_strcmp(key->data, "__waf_under_attack_uid") == 0) {
                    __waf_under_attack_uid.data = ngx_pnalloc(r->pool, sizeof(u_char) * (value->len + 1));
                    ngx_memzero(__waf_under_attack_uid.data, sizeof(u_char) * (value->len + 1));
                    ngx_memcpy(__waf_under_attack_uid.data, value->data, sizeof(u_char) * value->len);
                    __waf_under_attack_uid.len = value->len;
                } else if (ngx_strcmp(key->data, "__waf_under_attack_verification") == 0) {
                    __waf_under_attack_verification.data = ngx_pnalloc(r->pool, sizeof(u_char) * (value->len + 1));
                    ngx_memzero(__waf_under_attack_verification.data, sizeof(u_char) * (value->len + 1));
                    ngx_memcpy(__waf_under_attack_verification.data, value->data, sizeof(u_char) * value->len);
                    __waf_under_attack_verification.len = value->len;
                }
            }
            utarray_free(key_and_value);
        }

        utarray_free(cookies);
    }


    /* 如果 cookie 不完整 */
    if (__waf_under_attack_time.data == NULL || __waf_under_attack_uid.data == NULL || __waf_under_attack_verification.data == NULL) {
        ngx_http_waf_gen_cookie(r);
        *out_http_status = 303;
        ngx_http_waf_gen_ctx_and_header_location(r);
        return NGX_HTTP_WAF_MATCHED;
    }


    /* 验证 token 是否正确 */
    u_char cur_verification[65];
    ngx_memzero(cur_verification, sizeof(u_char) * 65);
    ngx_http_waf_gen_verification(r,
                                  __waf_under_attack_uid.data,
                                  NGX_HTTP_WAF_UNDER_ATTACH_UID_LEN,
                                  cur_verification,
                                  65,
                                  __waf_under_attack_time.data,
                                  NGX_HTTP_WAF_UNDER_ATTACH_TIME_LEN);
    if (ngx_memcmp(__waf_under_attack_verification.data, cur_verification, sizeof(u_char) * 64) != 0) {
        ngx_http_waf_gen_cookie(r);
        *out_http_status = 303;
        ngx_http_waf_gen_ctx_and_header_location(r);
        return NGX_HTTP_WAF_MATCHED;
    }


    /* 验证时间是否超过 5 秒 */
    time_t target_time = ngx_atoi(__waf_under_attack_time.data, __waf_under_attack_time.len);
    if (target_time == NGX_ERROR) {
        ngx_http_waf_gen_cookie(r);
        *out_http_status = 303;
        ngx_http_waf_gen_ctx_and_header_location(r);
        return NGX_HTTP_WAF_MATCHED;
    } else if (difftime(time(NULL), target_time) <= 5) {
        *out_http_status = 303;
        ngx_http_waf_gen_ctx_and_header_location(r);
        return NGX_HTTP_WAF_MATCHED;
    }

    return NGX_HTTP_WAF_NOT_MATCHED;
}


static ngx_int_t ngx_http_waf_gen_cookie(ngx_http_request_t *r) {
    static size_t s_header_key_len = sizeof("Set-Cookie");

    ngx_table_elt_t *__waf_under_attack_time = NULL;
    ngx_table_elt_t *__waf_under_attack_uid = NULL;
    ngx_table_elt_t *__waf_under_attack_verification = NULL;
    int write_len = 0;
    time_t now = time(NULL);
    u_char now_str[NGX_HTTP_WAF_UNDER_ATTACH_TIME_LEN + 1];
    ngx_memzero(now_str, sizeof(u_char) * (NGX_HTTP_WAF_UNDER_ATTACH_TIME_LEN + 1));
    sprintf((char*)now_str, "%ld", now);

    __waf_under_attack_time = (ngx_table_elt_t *)ngx_list_push(&(r->headers_out.headers));
    __waf_under_attack_time->hash = 1;
    __waf_under_attack_time->key.data = (u_char *)ngx_pnalloc(r->pool, sizeof(u_char) * (s_header_key_len + 1));
    ngx_memzero(__waf_under_attack_time->key.data, sizeof(u_char) * (s_header_key_len + 1));
    __waf_under_attack_time->key.len = s_header_key_len - 1;
    strcpy((char *)(__waf_under_attack_time->key.data), "Set-Cookie");

    __waf_under_attack_time->value.data = (u_char *)ngx_pnalloc(r->pool, sizeof(u_char) * 65);
    ngx_memzero(__waf_under_attack_time->value.data, sizeof(u_char) * 65);
    write_len = sprintf((char *)(__waf_under_attack_time->value.data), "__waf_under_attack_time=%s; Path=/", (char*)now_str);
    __waf_under_attack_time->value.len = (size_t)write_len;


    __waf_under_attack_uid = (ngx_table_elt_t *)ngx_list_push(&(r->headers_out.headers));
    __waf_under_attack_uid->hash = 1;
    __waf_under_attack_uid->key.data = (u_char *)ngx_pnalloc(r->pool, sizeof(u_char) * s_header_key_len);
    ngx_memzero(__waf_under_attack_uid->key.data, sizeof(u_char) * (s_header_key_len + 1));
    __waf_under_attack_uid->key.len = s_header_key_len - 1;
    strcpy((char *)(__waf_under_attack_uid->key.data), "Set-Cookie");

    __waf_under_attack_uid->value.data = (u_char *)ngx_pnalloc(r->pool, sizeof(u_char) * NGX_HTTP_WAF_UNDER_ATTACH_UID_LEN * 2);
    ngx_memzero(__waf_under_attack_uid->value.data, sizeof(u_char) * NGX_HTTP_WAF_UNDER_ATTACH_UID_LEN * 2);
    u_char* uid = ngx_pnalloc(r->pool, sizeof(u_char) * (NGX_HTTP_WAF_UNDER_ATTACH_UID_LEN + 1));
    rand_str(uid, 65);
    write_len = sprintf((char *)(__waf_under_attack_uid->value.data)
                        , "__waf_under_attack_uid=%s; Path=/",
                        (char *)uid);
    
    __waf_under_attack_uid->value.len = (size_t)write_len;


    __waf_under_attack_verification = (ngx_table_elt_t *)ngx_list_push(&(r->headers_out.headers));
    __waf_under_attack_verification->hash = 1;
    __waf_under_attack_verification->key.data = (u_char *)ngx_pnalloc(r->pool, sizeof(u_char) * s_header_key_len);
    ngx_memzero(__waf_under_attack_verification->key.data, sizeof(u_char) * (s_header_key_len + 1));
    __waf_under_attack_verification->key.len = s_header_key_len - 1;
    strcpy((char *)(__waf_under_attack_verification->key.data), "Set-Cookie");

    __waf_under_attack_verification->value.data = (u_char *)ngx_pnalloc(r->pool, sizeof(u_char) * 65);
    u_char* verification = ngx_pnalloc(r->pool, sizeof(u_char) * 65);
    ngx_memzero(__waf_under_attack_verification->value.data, sizeof(u_char) * 65);
    ngx_memzero(verification, sizeof(u_char) * 65);
    ngx_http_waf_gen_verification(r,
                                  uid,
                                  NGX_HTTP_WAF_UNDER_ATTACH_UID_LEN,
                                  verification,
                                  65,
                                  now_str,
                                  NGX_HTTP_WAF_UNDER_ATTACH_TIME_LEN);
    write_len = sprintf((char *)(__waf_under_attack_verification->value.data),
                        "__waf_under_attack_verification=%s; Path=/", (char*)verification);
    
    ngx_pfree(r->pool, verification);
    ngx_pfree(r->pool, uid);
    __waf_under_attack_verification->value.len = (size_t)write_len;

    return NGX_HTTP_WAF_TRUE;
}


static ngx_int_t ngx_http_waf_gen_verification(ngx_http_request_t *r, 
                                                u_char* uid, 
                                                size_t uid_len, 
                                                u_char* dst, 
                                                size_t dst_len, 
                                                u_char* now,
                                                size_t now_len) {
    ngx_http_waf_srv_conf_t *srv_conf = (ngx_http_waf_srv_conf_t *)ngx_http_get_module_srv_conf(r, ngx_http_waf_module);
    size_t buf_len = sizeof(srv_conf->random_str) + sizeof(inx_addr_t) + uid_len + now_len;
    u_char *buf = (u_char *)ngx_pnalloc(r->pool, buf_len);
    ngx_memzero(buf, sizeof(u_char) * buf_len);
    inx_addr_t inx_addr;
    ngx_memzero(&inx_addr, sizeof(inx_addr));

    if (r->connection->sockaddr->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)r->connection->sockaddr;
        ngx_memcpy(&(inx_addr.ipv4), &(sin->sin_addr), sizeof(struct in_addr));

    } else if (r->connection->sockaddr->sa_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)r->connection->sockaddr;
        ngx_memcpy(&(inx_addr.ipv6), &(sin6->sin6_addr), sizeof(struct in6_addr));
    }

    
    size_t offset = 0;

    /* 写入随机字符串 */
    ngx_memcpy(buf+ offset, srv_conf->random_str, sizeof(srv_conf->random_str));
    offset += sizeof(srv_conf->random_str);

    /* 写入时间戳 */
    ngx_memcpy(buf + offset, now, sizeof(u_char) * now_len);
    offset += now_len;

    /* 写入 uid */
    ngx_memcpy(buf + offset, uid, sizeof(u_char) * uid_len);
    offset += uid_len;

    /* 写入 IP 地址 */
    ngx_memcpy(buf + offset, &inx_addr, sizeof(inx_addr_t));
    offset += sizeof(inx_addr_t);

    ngx_int_t ret = sha256(dst, dst_len, buf, buf_len);
    ngx_pfree(r->pool, buf);

    return ret;
}


static void ngx_http_waf_gen_ctx_and_header_location(ngx_http_request_t *r) {
    size_t s_header_location_key_len = sizeof("Location");
    ngx_http_waf_srv_conf_t *srv_conf = (ngx_http_waf_srv_conf_t *)ngx_http_get_module_srv_conf(r, ngx_http_waf_module);

    
    ngx_table_elt_t* header = (ngx_table_elt_t *)ngx_list_push(&(r->headers_out.headers));
    header->hash = 1;
    header->key.data = ngx_pnalloc(r->pool, sizeof(u_char) * (s_header_location_key_len + 1));
    ngx_memzero(header->key.data, sizeof(u_char) * (s_header_location_key_len + 1));
    ngx_memcpy(header->key.data, "Location", s_header_location_key_len - 1);
    header->key.len = s_header_location_key_len - 1;

    header->value.data = ngx_pnalloc(r->pool, sizeof(u_char) * (srv_conf->waf_under_attack_uri.len + r->uri.len + 32));
    ngx_memzero(header->value.data, sizeof(u_char) * (srv_conf->waf_under_attack_uri.len + r->uri.len + 1));
    u_char* uri = ngx_pnalloc(r->pool, sizeof(u_char) * (r->uri.len + 1));
    ngx_memzero(uri, sizeof(u_char) * (r->uri.len + 1));
    ngx_memcpy(uri, r->uri.data, sizeof(u_char) * r->uri.len);
    header->value.len = sprintf((char*)header->value.data, "%s?target=%s",
            (char*)srv_conf->waf_under_attack_uri.data,
            (char*)uri);
    ngx_pfree(r->pool, uri);

    ngx_http_waf_ctx_t* ctx = ngx_http_get_module_ctx(r, ngx_http_waf_module);
    ctx->blocked = NGX_HTTP_WAF_TRUE;
    strcpy((char*)ctx->rule_type, "UNDER-ATTACK");
    ctx->rule_deatils[0] = '\0';
}


#endif