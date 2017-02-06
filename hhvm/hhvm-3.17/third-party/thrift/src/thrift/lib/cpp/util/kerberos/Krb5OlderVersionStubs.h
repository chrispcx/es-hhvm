/*
 * Copyright 2014 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef KRB5_OLDER_VERSION_STUBS
#define KRB5_OLDER_VERSION_STUBS

#include <gssapi/gssapi_generic.h>
#include <gssapi/gssapi_krb5.h>
#include <krb5.h>

// Proxy for older krb5 builds
#ifndef KRB5_GC_NO_STORE
extern "C" {
OM_uint32 gss_krb5_import_cred(
    OM_uint32 *minor_status,
    krb5_ccache id,
    krb5_principal keytab_principal,
    krb5_keytab keytab,
    gss_cred_id_t *cred);

krb5_boolean krb5_is_config_principal(
  krb5_context context, krb5_const_principal principal);
}
#endif

// If krb5_init_thread_local_context is not defined, replace it with
// a stub.
#ifndef KRB5_HAS_INIT_THREAD_LOCAL_CONTEXT
krb5_error_code krb5_init_thread_local_context(krb5_context *context);
#endif

#endif
