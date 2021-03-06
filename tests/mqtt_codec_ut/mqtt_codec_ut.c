// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "testrunnerswitcher.h"
#include "umock_c.h"

#ifdef __cplusplus
extern "C" {
#endif

    void* my_gballoc_malloc(size_t size)
    {
        return malloc(size);
    }

    void my_gballoc_free(void* ptr)
    {
        free(ptr);
    }

#ifdef __cplusplus
}
#endif

#define ENABLE_MOCKS

#include "azure_c_shared_utility/buffer_.h"
#include "azure_c_shared_utility/gballoc.h"

#undef ENABLE_MOCKS

#include "azure_umqtt_c/mqtt_codec.h"

static bool g_fail_alloc_calls;
static bool g_callbackInvoked;
static CONTROL_PACKET_TYPE g_curr_packet_type;
static const char* TEST_SUBSCRIPTION_TOPIC = "subTopic";
static const char* TEST_CLIENT_ID = "single_threaded_test";
static const char* TEST_TOPIC_NAME = "topic Name";
static const char* TEST_WILL_MSG = "Will Msg";
static const char* TEST_WILL_TOPIC = "Will Topic";
static const uint8_t* TEST_MESSAGE = (const uint8_t*)"Message to send";
static const uint16_t TEST_MESSAGE_LEN = 15;

static const char* TEST_PASSWORD = "SharedAccessSignature sr=iot-sdks-test.azure-devices.net&sig=VT%2bGaJ72cpMojDbR6l81ubjORboMY97fb3%2bzoRQtf6g%3d&se=1484433462&skn=iothubowner";

static SUBSCRIBE_PAYLOAD TEST_SUBSCRIBE_PAYLOAD[] = { { "subTopic1", DELIVER_AT_LEAST_ONCE },{ "subTopic2", DELIVER_EXACTLY_ONCE } };
static const char* TEST_UNSUBSCRIPTION_TOPIC[] = { "subTopic1", "subTopic2" };
static const char* TOPIC_NAME_A = "msgA";
static const uint8_t* APP_NAME_A = (const uint8_t*)"This is the app msg A.";
static size_t APP_NAME_A_LEN = 22;

#define TEST_HANDLE             0x11
#define TEST_PACKET_ID          0x1234
#define TEST_CALL_CONTEXT       0x1235
#define TEST_LIST_ITEM_HANDLE   0x1236
#define FIXED_HEADER_SIZE       2
#define OVER_MAX_SEND_SIZE      0xFFFFFF8F

typedef struct TEST_COMPLETE_DATA_INSTANCE_TAG
{
    unsigned char* dataHeader;
    size_t Length;
} TEST_COMPLETE_DATA_INSTANCE;

TEST_DEFINE_ENUM_TYPE(CONTROL_PACKET_TYPE, CONTROL_PACKET_TYPE_VALUES);
IMPLEMENT_UMOCK_C_ENUM_TYPE(CONTROL_PACKET_TYPE, CONTROL_PACKET_TYPE_VALUES);

static void SetupMqttLibOptions(MQTT_CLIENT_OPTIONS* options, const char* clientId,
    const char* willMsg,
    const char* willTopic,
    const char* username,
    const char* password,
    uint16_t keepAlive,
    bool messageRetain,
    bool cleanSession,
    QOS_VALUE qos)
{
    options->clientId = (char*)clientId;
    options->willMessage = (char*)willMsg;
    options->willTopic = (char*)willTopic;
    options->username = (char*)username;
    options->password = (char*)password;
    options->keepAliveInterval = keepAlive;
    options->messageRetain = messageRetain;
    options->useCleanSession = cleanSession;
    options->qualityOfServiceValue = qos;
}

#ifdef __cplusplus
extern "C" {
#endif

extern BUFFER_HANDLE real_BUFFER_new(void);
extern int real_BUFFER_build(BUFFER_HANDLE handle, const unsigned char* source, size_t size);
extern int real_BUFFER_enlarge(BUFFER_HANDLE handle, size_t enlargeSize);
extern int real_BUFFER_pre_build(BUFFER_HANDLE handle, size_t size);
extern int real_BUFFER_prepend(BUFFER_HANDLE handle1, BUFFER_HANDLE handle2);
extern void real_BUFFER_delete(BUFFER_HANDLE s);
extern unsigned char* real_BUFFER_u_char(BUFFER_HANDLE handle);
extern size_t real_BUFFER_length(BUFFER_HANDLE handle);

#ifdef __cplusplus
}
#endif

TEST_MUTEX_HANDLE test_serialize_mutex;

DEFINE_ENUM_STRINGS(UMOCK_C_ERROR_CODE, UMOCK_C_ERROR_CODE_VALUES)

static void on_umock_c_error(UMOCK_C_ERROR_CODE error_code)
{
    char temp_str[256];
    (void)snprintf(temp_str, sizeof(temp_str), "umock_c reported error :%s", ENUM_TO_STRING(UMOCK_C_ERROR_CODE, error_code));
    ASSERT_FAIL(temp_str);
}

BEGIN_TEST_SUITE(mqtt_codec_ut)

TEST_SUITE_INITIALIZE(suite_init)
{
    test_serialize_mutex = TEST_MUTEX_CREATE();
    ASSERT_IS_NOT_NULL(test_serialize_mutex);

    umock_c_init(on_umock_c_error);

    REGISTER_UMOCK_ALIAS_TYPE(BUFFER_HANDLE, void*);

    REGISTER_GLOBAL_MOCK_HOOK(gballoc_malloc, my_gballoc_malloc);
    REGISTER_GLOBAL_MOCK_HOOK(gballoc_free, my_gballoc_free);
    REGISTER_GLOBAL_MOCK_HOOK(BUFFER_build, real_BUFFER_build);
    REGISTER_GLOBAL_MOCK_HOOK(BUFFER_new, real_BUFFER_new);
    REGISTER_GLOBAL_MOCK_HOOK(BUFFER_build, real_BUFFER_build);
    REGISTER_GLOBAL_MOCK_HOOK(BUFFER_enlarge, real_BUFFER_enlarge);
    REGISTER_GLOBAL_MOCK_HOOK(BUFFER_pre_build, real_BUFFER_pre_build);
    REGISTER_GLOBAL_MOCK_HOOK(BUFFER_prepend, real_BUFFER_prepend);
    REGISTER_GLOBAL_MOCK_HOOK(BUFFER_delete, real_BUFFER_delete);
    REGISTER_GLOBAL_MOCK_HOOK(BUFFER_u_char, real_BUFFER_u_char);
    REGISTER_GLOBAL_MOCK_HOOK(BUFFER_length, real_BUFFER_length);
}

TEST_SUITE_CLEANUP(suite_cleanup)
{
    umock_c_deinit();

    TEST_MUTEX_DESTROY(test_serialize_mutex);
}

TEST_FUNCTION_INITIALIZE(method_init)
{
    if (TEST_MUTEX_ACQUIRE(test_serialize_mutex))
    {
        ASSERT_FAIL("Could not acquire test serialization mutex.");
    }
    g_fail_alloc_calls = false;
    g_callbackInvoked = false;

    umock_c_reset_all_calls();
}

TEST_FUNCTION_CLEANUP(method_cleanup)
{
    TEST_MUTEX_RELEASE(test_serialize_mutex);
}

static void PrintLogFunction(unsigned int options, char* format, ...)
{
    (void)options;
    (void)format;
}

static void TestOnCompleteCallback(void* context, CONTROL_PACKET_TYPE packet, int flags, BUFFER_HANDLE headerData)
{
    TEST_COMPLETE_DATA_INSTANCE* testData = (TEST_COMPLETE_DATA_INSTANCE*)context;
    (void)flags;
    if (testData != NULL)
    {
        if (packet == PINGRESP_TYPE)
        {
            g_callbackInvoked = true;
        }
        else if (testData->Length > 0 && testData->dataHeader != NULL)
        {
            if (memcmp(testData->dataHeader, real_BUFFER_u_char(headerData), testData->Length) == 0)
            {
                g_callbackInvoked = true;
            }
        }
    }
}

/* Tests_SRS_MQTT_CODEC_07_002: [On success mqtt_codec_create shall return a MQTTCODEC_HANDLE value.] */
TEST_FUNCTION(mqtt_codec_create_succeed)
{
    // arrange
    EXPECTED_CALL(gballoc_malloc(IGNORED_NUM_ARG));

    // act
    MQTTCODEC_HANDLE handle = mqtt_codec_create(TestOnCompleteCallback, NULL);

    // assert
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    mqtt_codec_destroy(handle);
}

/* Tests_SRS_MQTT_CODEC_07_004: [mqtt_codec_destroy shall deallocate all memory that has been allocated by this object.] */
TEST_FUNCTION(mqtt_codec_destroy_succeed)
{
    // arrange
    MQTTCODEC_HANDLE handle = mqtt_codec_create(TestOnCompleteCallback, NULL);
    umock_c_reset_all_calls();

    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));
    EXPECTED_CALL(gballoc_free(IGNORED_PTR_ARG));

    // act
    mqtt_codec_destroy(handle);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
}

/* Tests_SRS_MQTT_CODEC_07_003: [If the handle parameter is NULL then mqtt_codec_destroy shall do nothing.] */
TEST_FUNCTION(mqtt_codec_destroy_handle_NULL_fail)
{
    // arrange

    // act
    mqtt_codec_destroy(NULL);

    // assert
}

/* Tests_SRS_MQTT_CODEC_07_008: [If the parameters mqttOptions is NULL then mqtt_codec_connect shall return a null value.] */
TEST_FUNCTION(mqtt_codec_connect_MQTTCLIENT_OPTIONS_NULL_fail)
{
    // arrange

    // act
    BUFFER_HANDLE handle = mqtt_codec_connect(NULL);

    // assert
    ASSERT_IS_NULL(handle);
}

/* Tests_SRS_MQTT_CODEC_07_010: [If any error is encountered then mqtt_codec_connect shall return NULL.] */
TEST_FUNCTION(mqtt_codec_connect_BUFFER_enlarge_fail)
{
    // arrange
    MQTT_CLIENT_OPTIONS mqttOptions = { 0 };
    SetupMqttLibOptions(&mqttOptions, TEST_CLIENT_ID, NULL, NULL, "testuser", "testpassword", 20, false, true, DELIVER_AT_MOST_ONCE);

    STRICT_EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG)).SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_connect(&mqttOptions);

    // assert
    ASSERT_IS_NULL(handle);
}

TEST_FUNCTION(mqtt_codec_connect_WillMsg_zero_WillTopic_nonzero_fail)
{
    // arrange
    MQTT_CLIENT_OPTIONS mqttOptions = { 0 };
    SetupMqttLibOptions(&mqttOptions, TEST_CLIENT_ID, TEST_WILL_MSG, NULL, "testuser", "testpassword", 20, false, true, DELIVER_AT_MOST_ONCE);

    STRICT_EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG)).SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_connect(&mqttOptions);

    // assert
    ASSERT_IS_NULL(handle);
}

TEST_FUNCTION(mqtt_codec_connect_second_succeeds)
{
    // arrange
    MQTT_CLIENT_OPTIONS mqttOptions = { 0 };
    SetupMqttLibOptions(&mqttOptions, TEST_CLIENT_ID, TEST_WILL_MSG, TEST_WILL_TOPIC, NULL, NULL, 20, true, true, DELIVER_AT_MOST_ONCE);

    const unsigned char CONNECT_VALUE[] = { 0x10, 0x36, 0x00, 0x04, 0x4d, 0x51, 0x54, 0x54, 0x04, 0x26, 0x00, 0x14, 0x00, 0x14, 0x73, 0x69, \
        0x6e, 0x67, 0x6c, 0x65, 0x5f, 0x74, 0x68, 0x72, 0x65, 0x61, 0x64, 0x65, 0x64, 0x5f, 0x74, 0x65, 0x73, 0x74, 0x00, 0x0a, 0x57, 0x69, \
        0x6c, 0x6c, 0x20, 0x54, 0x6f, 0x70, 0x69, 0x63, 0x00, 0x08, 0x57, 0x69, 0x6c, 0x6c, 0x20, 0x4d, 0x73, 0x67 };

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_prepend(IGNORED_PTR_ARG, IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_connect(&mqttOptions);

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, CONNECT_VALUE, length));

    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    real_BUFFER_delete(handle);
}

/* Tests_SRS_MQTT_CODEC_07_009: [mqtt_codec_connect shall construct a BUFFER_HANDLE that represents a MQTT CONNECT packet.] */
TEST_FUNCTION(mqtt_codec_connect_succeeds)
{
    // arrange
    MQTT_CLIENT_OPTIONS mqttOptions = { 0 };
    SetupMqttLibOptions(&mqttOptions, TEST_CLIENT_ID, NULL, NULL, "testuser", "testpassword", 20, false, true, DELIVER_AT_MOST_ONCE);

    const unsigned char CONNECT_VALUE[] = { 0x10, 0x38, 0x00, 0x04, 0x4d, 0x51, 0x54, 0x54, 0x04, 0xc2, 0x00, 0x14, 0x00, 0x14, 0x73, 0x69, \
        0x6e, 0x67, 0x6c, 0x65, 0x5f, 0x74, 0x68, 0x72, 0x65, 0x61, 0x64, 0x65, 0x64, 0x5f, 0x74, 0x65, 0x73, 0x74, 0x00, 0x08, 0x74, \
        0x65, 0x73, 0x74, 0x75, 0x73, 0x65, 0x72, 0x00, 0x0c, 0x74, 0x65, 0x73, 0x74, 0x70, 0x61, 0x73, 0x73, 0x77, 0x6f, 0x72, 0x64 };

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_prepend(IGNORED_PTR_ARG, IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_connect(&mqttOptions);

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, CONNECT_VALUE, length));

    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    real_BUFFER_delete(handle);
}

/* Tests_SRS_MQTT_CODEC_07_009: [mqtt_codec_connect shall construct a BUFFER_HANDLE that represents a MQTT CONNECT packet.] */
TEST_FUNCTION(mqtt_codec_connect_no_password_succeeds)
{
    // arrange
    MQTT_CLIENT_OPTIONS mqttOptions ={ 0 };
    SetupMqttLibOptions(&mqttOptions, TEST_CLIENT_ID, NULL, NULL, "testuser", NULL, 20, false, true, DELIVER_AT_MOST_ONCE);

    const unsigned char CONNECT_VALUE[] = { 0x10, 0x2a, 0x00, 0x04, 0x4d, 0x51, 0x54, 0x54, 0x04, 0x82, 0x00, 0x14, 0x00, 0x14, 0x73, 0x69, \
        0x6e, 0x67, 0x6c, 0x65, 0x5f, 0x74, 0x68, 0x72, 0x65, 0x61, 0x64, 0x65, 0x64, 0x5f, 0x74, 0x65, 0x73, 0x74, 0x00, 0x08, 0x74, \
        0x65, 0x73, 0x74, 0x75, 0x73, 0x65, 0x72, 0x00, 0x00 };

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_prepend(IGNORED_PTR_ARG, IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_connect(&mqttOptions);

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, CONNECT_VALUE, length));

    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    real_BUFFER_delete(handle);
}

/* Tests_SRS_MQTT_CODEC_07_009: [mqtt_codec_connect shall construct a BUFFER_HANDLE that represents a MQTT CONNECT packet.] */
TEST_FUNCTION(mqtt_codec_connect_Large_Data_succeeds)
{
    // arrange
    MQTT_CLIENT_OPTIONS mqttOptions = { 0 };
    SetupMqttLibOptions(&mqttOptions, TEST_CLIENT_ID, NULL, NULL, "testuser.testusersuffix/deviceId", TEST_PASSWORD, 20, false, true, DELIVER_AT_MOST_ONCE);

    const unsigned char CONNECT_VALUE[] = { 0x10, 0xd1, 0x01, 0x00, 0x04, 0x4d, 0x51, 0x54, 0x54, 0x04, 0xc2, 0x00, 0x14, 0x00, 0x14, 0x73, 0x69, 0x6e, 0x67, 0x6c, 0x65, 0x5f, 0x74, \
        0x68, 0x72, 0x65, 0x61, 0x64, 0x65, 0x64, 0x5f, 0x74, 0x65, 0x73, 0x74, 0x00, 0x20, 0x74, 0x65, 0x73, 0x74, 0x75, 0x73, 0x65, 0x72, 0x2e, 0x74, 0x65, 0x73, 0x74, 0x75, 0x73, \
        0x65, 0x72, 0x73, 0x75, 0x66, 0x66, 0x69, 0x78, 0x2f, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x49, 0x64, 0x00, 0x8d, 0x53, 0x68, 0x61, 0x72, 0x65, 0x64, 0x41, 0x63, 0x63, 0x65, \
        0x73, 0x73, 0x53, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x20, 0x73, 0x72, 0x3d, 0x69, 0x6f, 0x74, 0x2d, 0x73, 0x64, 0x6b, 0x73, 0x2d, 0x74, 0x65, 0x73, 0x74, 0x2e, \
        0x61, 0x7a, 0x75, 0x72, 0x65, 0x2d, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x73, 0x2e, 0x6e, 0x65, 0x74, 0x26, 0x73, 0x69, 0x67, 0x3d, 0x56, 0x54, 0x25, 0x32, 0x62, 0x47, 0x61, \
        0x4a, 0x37, 0x32, 0x63, 0x70, 0x4d, 0x6f, 0x6a, 0x44, 0x62, 0x52, 0x36, 0x6c, 0x38, 0x31, 0x75, 0x62, 0x6a, 0x4f, 0x52, 0x62, 0x6f, 0x4d, 0x59, 0x39, 0x37, 0x66, 0x62, 0x33, \
        0x25, 0x32, 0x62, 0x7a, 0x6f, 0x52, 0x51, 0x74, 0x66, 0x36, 0x67, 0x25, 0x33, 0x64, 0x26, 0x73, 0x65, 0x3d, 0x31, 0x34, 0x38, 0x34, 0x34, 0x33, 0x33, 0x34, 0x36, 0x32, 0x26, \
        0x73, 0x6b, 0x6e, 0x3d, 0x69, 0x6f, 0x74, 0x68, 0x75, 0x62, 0x6f, 0x77, 0x6e, 0x65, 0x72
    };

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_prepend(IGNORED_PTR_ARG, IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_connect(&mqttOptions);

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, CONNECT_VALUE, length));

    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    real_BUFFER_delete(handle);
}

/* Tests_SRS_MQTT_CODEC_07_011: [On success mqtt_codec_disconnect shall construct a BUFFER_HANDLE that represents a MQTT DISCONNECT packet.] */
TEST_FUNCTION(mqtt_codec_disconnect_succeed)
{
    // arrange
    const unsigned char DISCONNECT_VALUE[] = { 0xE0, 0x00 };

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_disconnect();

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, DISCONNECT_VALUE, length));

    // cleanup
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    real_BUFFER_delete(handle);
}

/* Tests_SRS_MQTT_CODEC_07_012: [If any error is encountered mqtt_codec_disconnect shall return NULL.] */
TEST_FUNCTION(mqtt_codec_disconnect_BUFFER_enlarge_fails)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG)).SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_disconnect();

    // assert
    ASSERT_IS_NULL(handle);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
}

/* Tests_SRS_MQTT_CODEC_07_022: [If any error is encountered mqtt_codec_ping shall return NULL.] */
TEST_FUNCTION(mqtt_codec_ping_BUFFER_enlarge_fails)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG)).SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_ping();

    // assert
    ASSERT_IS_NULL(handle);
}

/* Tests_SRS_MQTT_CODEC_07_021: [On success mqtt_codec_ping shall construct a BUFFER_HANDLE that represents a MQTT PINGREQ packet.] */
TEST_FUNCTION(mqtt_codec_ping_succeeds)
{
    // arrange
    const unsigned char PING_VALUE[] = { 0xC0, 0x00 };

    STRICT_EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_ping();

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, PING_VALUE, length));
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    real_BUFFER_delete(handle);
}

/* Tests_SRS_MQTT_CODEC_07_005: [If the parameters topicName is NULL then mqtt_codec_publish shall return NULL.] */
TEST_FUNCTION(mqtt_codec_publish_topicName_NULL_fail)
{
    // arrange

    // act
    BUFFER_HANDLE handle = mqtt_codec_publish(DELIVER_AT_MOST_ONCE, true, false, TEST_PACKET_ID, NULL, TEST_MESSAGE, TEST_MESSAGE_LEN);

    // assert
    ASSERT_IS_NULL(handle);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
}

/* Tests_SRS_CONTROL_PACKET_07_052: [mqtt_codec_publish shall constuct the MQTT variable header and shall return a non-zero value on failure.] */
TEST_FUNCTION(mqtt_codec_publish_construct_BUFFER_enlarge_fail)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG)).SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publish(DELIVER_AT_MOST_ONCE, true, false, TEST_PACKET_ID, TEST_TOPIC_NAME, TEST_MESSAGE, TEST_MESSAGE_LEN);

    // assert
    ASSERT_IS_NULL(handle);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
}

/* Tests_SRS_MQTT_CODEC_07_006: [If any error is encountered then mqtt_codec_publish shall return NULL.] */
TEST_FUNCTION(mqtt_codec_publish_BUFFER_enlarge_fails)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG)).SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publish(DELIVER_AT_LEAST_ONCE, true, false, TEST_PACKET_ID, TEST_TOPIC_NAME, TEST_MESSAGE, TEST_MESSAGE_LEN);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Tests_SRS_MQTT_CODEC_07_006: [If any error is encountered then mqtt_codec_publish shall return NULL.] */
TEST_FUNCTION(mqtt_codec_publish_constructFixedHeader_fails)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));

    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));

    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG)).SetReturn(__LINE__);

    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publish(DELIVER_AT_LEAST_ONCE, true, false, TEST_PACKET_ID, TEST_TOPIC_NAME, TEST_MESSAGE, TEST_MESSAGE_LEN);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Codes_SRS_MQTT_CODEC_07_005: [If the parameters topicName is NULL then mqtt_codec_publish shall return NULL.] */
TEST_FUNCTION(mqtt_codec_publish_msgBuffer_NULL_succeeds)
{
    // arrange
    const unsigned char PUBLISH_VALUE[] = { 0x38, 0x0c, 0x00, 0x0a, 0x74, 0x6f, 0x70, 0x69, 0x63, 0x20, 0x4e, 0x61, 0x6d, 0x65 };

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));

    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_prepend(IGNORED_PTR_ARG, IGNORED_PTR_ARG));

    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publish(DELIVER_AT_MOST_ONCE, true, false, TEST_PACKET_ID, TEST_TOPIC_NAME, NULL, 0);

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, PUBLISH_VALUE, length));
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    real_BUFFER_delete(handle);
}

/* Tests_SRS_MQTT_CODEC_07_007: [mqtt_codec_publish shall return a BUFFER_HANDLE that represents a MQTT PUBLISH message.] */
TEST_FUNCTION(mqtt_codec_publish_succeeds)
{
    // arrange
    const unsigned char PUBLISH_VALUE[] = { 0x3a, 0x1d, 0x00, 0x0a, 0x74, 0x6f, 0x70, 0x69, 0x63, 0x20, 0x4e, 0x61, 0x6d, 0x65, 0x12, 0x34, 0x4d, 0x65, \
        0x73, 0x73, 0x61, 0x67, 0x65, 0x20, 0x74, 0x6f, 0x20, 0x73, 0x65, 0x6e, 0x64 };

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));

    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_prepend(IGNORED_PTR_ARG, IGNORED_PTR_ARG));

    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publish(DELIVER_AT_LEAST_ONCE, true, false, TEST_PACKET_ID, TEST_TOPIC_NAME, TEST_MESSAGE, TEST_MESSAGE_LEN);

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, PUBLISH_VALUE, length));
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    real_BUFFER_delete(handle);
}

/* Tests_SRS_MQTT_CODEC_07_007: [mqtt_codec_publish shall return a BUFFER_HANDLE that represents a MQTT PUBLISH message.] */
TEST_FUNCTION(mqtt_codec_publish_second_succeeds)
{
    // arrange
    const unsigned char PUBLISH_VALUE[] = { 0x30, 0x1c, 0x00, 0x04, 0x6d, 0x73, 0x67, 0x41, 0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x61, 0x70, 0x70, 0x20, 0x6d, 0x73, 0x67, 0x20, 0x41, 0x2e };

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));

    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_prepend(IGNORED_PTR_ARG, IGNORED_PTR_ARG));

    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publish(DELIVER_AT_MOST_ONCE, false, false, 12, TOPIC_NAME_A, APP_NAME_A, APP_NAME_A_LEN);

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, PUBLISH_VALUE, length));

    // cleanup
    real_BUFFER_delete(handle);
}

/* Tests_SRS_MQTT_CODEC_07_013: [On success mqtt_codec_publishAck shall return a BUFFER_HANDLE representation of a MQTT PUBACK packet.] */
TEST_FUNCTION(mqtt_codec_publish_ack_pre_build_fail)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    STRICT_EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, 4))
        .IgnoreArgument(1)
        .SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publishAck(TEST_PACKET_ID);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Codes_SRS_MQTT_CODEC_07_036: [mqtt_codec_publish shall return NULL if the buffLen variable is greater than the MAX_SEND_SIZE (0xFFFFFF7F).] */
TEST_FUNCTION(mqtt_codec_publish_over_max_size_fail)
{
    // arrange

    // act
    BUFFER_HANDLE handle = mqtt_codec_publish(DELIVER_AT_LEAST_ONCE, true, false, TEST_PACKET_ID, TEST_TOPIC_NAME, TEST_MESSAGE, OVER_MAX_SEND_SIZE);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);

    // cleanup
    real_BUFFER_delete(handle);
}

/* Tests_SRS_MQTT_CODEC_07_014 : [If any error is encountered then mqtt_codec_publishAck shall return NULL.] */
TEST_FUNCTION(mqtt_codec_publish_ack_succeeds)
{
    // arrange
    unsigned char PUBLISH_ACK_VALUE[] = { 0x40, 0x02, 0x12, 0x34 };

    EXPECTED_CALL(BUFFER_new());
    STRICT_EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, 4))
        .IgnoreArgument(1);
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publishAck(TEST_PACKET_ID);

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, PUBLISH_ACK_VALUE, length));

    // cleanup
    real_BUFFER_delete(handle);
}

/* Codes_SRS_MQTT_CODEC_07_016 : [If any error is encountered then mqtt_codec_publishRecieved shall return NULL.] */
TEST_FUNCTION(mqtt_codec_publish_received_pre_build_fail)
{
    // arrange
    STRICT_EXPECTED_CALL(BUFFER_new());
    STRICT_EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, 4))
        .IgnoreArgument(1)
        .SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publishReceived(TEST_PACKET_ID);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Codes_SRS_MQTT_CODEC_07_015: [On success mqtt_codec_publishRecieved shall return a BUFFER_HANDLE representation of a MQTT PUBREC packet.] */
TEST_FUNCTION(mqtt_codec_publish_received_succeeds)
{
    // arrange
    unsigned char PUBLISH_ACK_VALUE[] = { 0x50, 0x02, 0x12, 0x34 };

    EXPECTED_CALL(BUFFER_new());
    STRICT_EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, 4))
        .IgnoreArgument(1);
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publishReceived(TEST_PACKET_ID);

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, PUBLISH_ACK_VALUE, length));

    // cleanup
    real_BUFFER_delete(handle);
}

/* Codes_SRS_MQTT_CODEC_07_018 : [If any error is encountered then mqtt_codec_publishRelease shall return NULL.] */
TEST_FUNCTION(mqtt_codec_publish_release_pre_build_fail)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    STRICT_EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, 4))
        .IgnoreArgument(1)
        .SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publishRelease(TEST_PACKET_ID);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Codes_SRS_MQTT_CODEC_07_017: [On success mqtt_codec_publishRelease shall return a BUFFER_HANDLE representation of a MQTT PUBREL packet.] */
TEST_FUNCTION(mqtt_codec_publish_release_succeeds)
{
    // arrange
    unsigned char PUBLISH_ACK_VALUE[] = { 0x62, 0x02, 0x12, 0x34 };

    EXPECTED_CALL(BUFFER_new());
    STRICT_EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, 4))
        .IgnoreArgument(1);
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publishRelease(TEST_PACKET_ID);

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, PUBLISH_ACK_VALUE, length));

    // cleanup
    real_BUFFER_delete(handle);
}

/* Codes_SRS_MQTT_CODEC_07_020 : [If any error is encountered then mqtt_codec_publishComplete shall return NULL.] */
TEST_FUNCTION(mqtt_codec_publish_complete_pre_build_fail)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    STRICT_EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, 4))
        .IgnoreArgument(1)
        .SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publishComplete(TEST_PACKET_ID);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Codes_SRS_MQTT_CODEC_07_019: [On success mqtt_codec_publishComplete shall return a BUFFER_HANDLE representation of a MQTT PUBCOMP packet.] */
TEST_FUNCTION(mqtt_codec_publish_complete_succeeds)
{
    // arrange
    unsigned char PUBLISH_COMP_VALUE[] = { 0x70, 0x02, 0x12, 0x34 };

    EXPECTED_CALL(BUFFER_new());
    STRICT_EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, 4))
        .IgnoreArgument(1);
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_publishComplete(TEST_PACKET_ID);

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, PUBLISH_COMP_VALUE, length));

    // cleanup
    real_BUFFER_delete(handle);
}

/* Codes_SRS_MQTT_CODEC_07_023: [If the parameters subscribeList is NULL or if count is 0 then mqtt_codec_subscribe shall return NULL.] */
TEST_FUNCTION(mqtt_codec_subscribe_subscribeList_NULL_fails)
{
    // arrange

    // act
    BUFFER_HANDLE handle = mqtt_codec_subscribe(TEST_PACKET_ID, NULL, 0);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Codes_SRS_MQTT_CODEC_07_025: [If any error is encountered then mqtt_codec_subscribe shall return NULL.] */
TEST_FUNCTION(mqtt_codec_subscribe_BUFFER_enlarge_fails)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG)).SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_subscribe(TEST_PACKET_ID, TEST_SUBSCRIBE_PAYLOAD, 2);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Codes_SRS_MQTT_CODEC_07_025: [If any error is encountered then mqtt_codec_subscribe shall return NULL.] */
TEST_FUNCTION(mqtt_codec_subscribe_addListItemsToSubscribePacket_fails)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG)).SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_subscribe(TEST_PACKET_ID, TEST_SUBSCRIBE_PAYLOAD, 2);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Codes_SRS_MQTT_CODEC_07_025: [If any error is encountered then mqtt_codec_subscribe shall return NULL.] */
TEST_FUNCTION(mqtt_codec_subscribe_constructFixedHeader_fails)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG)).SetReturn(__LINE__);

    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_subscribe(TEST_PACKET_ID, TEST_SUBSCRIBE_PAYLOAD, 2);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Codes_SRS_MQTT_CODEC_07_026: [mqtt_codec_subscribe shall return a BUFFER_HANDLE that represents a MQTT SUBSCRIBE message.]*/
/* Codes_SRS_MQTT_CODEC_07_024: [mqtt_codec_subscribe shall iterate through count items in the subscribeList.] */
TEST_FUNCTION(mqtt_codec_subscribe_succeeds)
{
    // arrange
    unsigned char SUBSCRIBE_VALUE[] = { 0x82, 0x1a, 0x12, 0x34, 0x00, 0x09, 0x73, 0x75, 0x62, 0x54, 0x6f, 0x70, 0x69, 0x63, 0x31, 0x01, 0x00, 0x09, 0x73, 0x75, 0x62, 0x54, 0x6f, 0x70, 0x69, 0x63, 0x32, 0x02 };

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_prepend(IGNORED_PTR_ARG, IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_subscribe(TEST_PACKET_ID, TEST_SUBSCRIBE_PAYLOAD, 2);

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, SUBSCRIBE_VALUE, length));

    // cleanup
    real_BUFFER_delete(handle);
}

/* Codes_SRS_MQTT_CODEC_07_027: [If the parameters unsubscribeList is NULL or if count is 0 then mqtt_codec_unsubscribe shall return NULL.] */
TEST_FUNCTION(mqtt_codec_unsubscribe_subscribeList_NULL_fails)
{
    // arrange

    // act
    BUFFER_HANDLE handle = mqtt_codec_unsubscribe(TEST_PACKET_ID, NULL, 0);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Codes_SRS_MQTT_CODEC_07_029: [If any error is encountered then mqtt_codec_unsubscribe shall return NULL.] */
TEST_FUNCTION(mqtt_codec_unsubscribe_BUFFER_enlarge_fails)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG)).SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_unsubscribe(TEST_PACKET_ID, TEST_UNSUBSCRIPTION_TOPIC, 2);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Codes_SRS_MQTT_CODEC_07_029: [If any error is encountered then mqtt_codec_unsubscribe shall return NULL.] */
TEST_FUNCTION(mqtt_codec_unsubscribe_addListItemToUnsubscribePacket_BUFFER_enlarge_fails)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG)).SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_unsubscribe(TEST_PACKET_ID, TEST_UNSUBSCRIPTION_TOPIC, 2);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Codes_SRS_MQTT_CODEC_07_029: [If any error is encountered then mqtt_codec_unsubscribe shall return NULL.] */
TEST_FUNCTION(mqtt_codec_unsubscribe_constructFixedHeader_fails)
{
    // arrange
    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG)).SetReturn(__LINE__);
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_unsubscribe(TEST_PACKET_ID, TEST_UNSUBSCRIPTION_TOPIC, 2);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NULL(handle);
}

/* Codes_SRS_MQTT_CODEC_07_030: [mqtt_codec_unsubscribe shall return a BUFFER_HANDLE that represents a MQTT SUBSCRIBE message.] */
TEST_FUNCTION(mqtt_codec_unsubscribe_succeeds)
{
    // arrange
    unsigned char UNSUBSCRIBE_VALUE[] = { 0xa2, 0x18, 0x12, 0x34, 0x00, 0x09, 0x73, 0x75, 0x62, 0x54, 0x6f, 0x70, 0x69, 0x63, 0x31, 0x00, 0x09, 0x73, 0x75, 0x62, 0x54, 0x6f, 0x70, 0x69, 0x63, 0x32 };

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_enlarge(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_prepend(IGNORED_PTR_ARG, IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));

    // act
    BUFFER_HANDLE handle = mqtt_codec_unsubscribe(TEST_PACKET_ID, TEST_UNSUBSCRIPTION_TOPIC, 2);

    unsigned char* data = real_BUFFER_u_char(handle);
    size_t length = BUFFER_length(handle);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_IS_NOT_NULL(handle);
    ASSERT_ARE_EQUAL(int, 0, memcmp(data, UNSUBSCRIBE_VALUE, length));

    // cleanup
    real_BUFFER_delete(handle);
}

/* Codes_SRS_MQTT_CODEC_07_031: [If the parameters handle or buffer is NULL then mqtt_codec_bytesReceived shall return a non-zero value.] */
TEST_FUNCTION(mqtt_codec_bytesReceived_MQTTCODEC_HANDLE_fails)
{
    // arrange
    unsigned char CONNACK_RESP[] = { 0x20, 0x2, 0x1, 0x0 };

    // act
    mqtt_codec_bytesReceived(NULL, CONNACK_RESP, 1);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
}

/* Codes_SRS_MQTT_CODEC_07_031: [If the parameters handle or buffer is NULL then mqtt_codec_bytesReceived shall return a non-zero value.] */
TEST_FUNCTION(mqtt_codec_bytesReceived_buffer_NULL_fails)
{
    // arrange
    MQTTCODEC_HANDLE handle = mqtt_codec_create(TestOnCompleteCallback, NULL);

    umock_c_reset_all_calls();

    // act
    mqtt_codec_bytesReceived(handle, NULL, 1);
    
    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    mqtt_codec_destroy(handle);
}

/* Codes_SRS_MQTT_CODEC_07_032: [If the parameters size is zero then mqtt_codec_bytesReceived shall return a non-zero value.] */
TEST_FUNCTION(mqtt_codec_bytesReceived_buffer_Len_0_fails)
{
    // arrange
    unsigned char CONNACK_RESP[] = { 0x20, 0x2, 0x1, 0x0 };

    MQTTCODEC_HANDLE handle = mqtt_codec_create(TestOnCompleteCallback, NULL);

    umock_c_reset_all_calls();

    // act
    mqtt_codec_bytesReceived(handle, CONNACK_RESP, 0);

    // assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    mqtt_codec_destroy(handle);
}

/* Codes_SRS_MQTT_CODEC_07_033: [mqtt_codec_bytesReceived constructs a sequence of bytes into the corresponding MQTT packets and on success returns zero.] */
/* Codes_SRS_MQTT_CODEC_07_034: [Upon a constructing a complete MQTT packet mqtt_codec_bytesReceived shall call the ON_PACKET_COMPLETE_CALLBACK function.] */
TEST_FUNCTION(mqtt_codec_bytesReceived_connack_succeed)
{
    // arrange
    unsigned char CONNACK_RESP[] = { 0x20, 0x2, 0x1, 0x0 };
    size_t length = sizeof(CONNACK_RESP) / sizeof(CONNACK_RESP[0]);

    TEST_COMPLETE_DATA_INSTANCE testData = { 0 };
    testData.dataHeader = CONNACK_RESP + FIXED_HEADER_SIZE;
    testData.Length = 2;

    MQTTCODEC_HANDLE handle = mqtt_codec_create(TestOnCompleteCallback, &testData);

    umock_c_reset_all_calls();

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    g_curr_packet_type = CONNACK_TYPE;

    // act
    for (size_t index = 0; index < length; index++)
    {
        // Send 1 byte at a time
        mqtt_codec_bytesReceived(handle, CONNACK_RESP+index, 1);
    }

    // assert
    ASSERT_IS_TRUE(g_callbackInvoked);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    mqtt_codec_destroy(handle);
}

TEST_FUNCTION(mqtt_codec_bytesReceived_connack_auth_reject_succeed)
{
    // arrange
    unsigned char CONNACK_RESP[] ={ 0x20, 0x2, 0x00, 0x05 };
    size_t length = sizeof(CONNACK_RESP) / sizeof(CONNACK_RESP[0]);

    TEST_COMPLETE_DATA_INSTANCE testData ={ 0 };
    testData.dataHeader = CONNACK_RESP + FIXED_HEADER_SIZE;
    testData.Length = 2;

    MQTTCODEC_HANDLE handle = mqtt_codec_create(TestOnCompleteCallback, &testData);

    umock_c_reset_all_calls();

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    g_curr_packet_type = CONNACK_TYPE;

    // act
    mqtt_codec_bytesReceived(handle, CONNACK_RESP, length);

    // assert
    ASSERT_IS_TRUE(g_callbackInvoked);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    mqtt_codec_destroy(handle);
}

/* Codes_SRS_MQTT_CODEC_07_033: [mqtt_codec_bytesReceived constructs a sequence of bytes into the corresponding MQTT packets and on success returns zero.] */
/* Codes_SRS_MQTT_CODEC_07_034: [Upon a constructing a complete MQTT packet mqtt_codec_bytesReceived shall call the ON_PACKET_COMPLETE_CALLBACK function.] */
TEST_FUNCTION(mqtt_codec_bytesReceived_puback_succeed)
{
    // arrange
    unsigned char PUBACK_RESP[] = { 0x40, 0x2, 0x12, 0x34 };
    size_t length = sizeof(PUBACK_RESP) / sizeof(PUBACK_RESP[0]);

    TEST_COMPLETE_DATA_INSTANCE testData = { 0 };
    testData.dataHeader = PUBACK_RESP + FIXED_HEADER_SIZE;
    testData.Length = length - FIXED_HEADER_SIZE;

    MQTTCODEC_HANDLE handle = mqtt_codec_create(TestOnCompleteCallback, &testData);

    umock_c_reset_all_calls();

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    g_curr_packet_type = PUBACK_TYPE;

    // act
    for (size_t index = 0; index < length; index++)
    {
        // Send 1 byte at a time
        mqtt_codec_bytesReceived(handle, PUBACK_RESP + index, 1);
    }

    // assert
    ASSERT_IS_TRUE(g_callbackInvoked);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    mqtt_codec_destroy(handle);
}

/* Codes_SRS_MQTT_CODEC_07_033: [mqtt_codec_bytesReceived constructs a sequence of bytes into the corresponding MQTT packets and on success returns zero.] */
/* Codes_SRS_MQTT_CODEC_07_034: [Upon a constructing a complete MQTT packet mqtt_codec_bytesReceived shall call the ON_PACKET_COMPLETE_CALLBACK function.] */
TEST_FUNCTION(mqtt_codec_bytesReceived_pingresp_succeed)
{
    // arrange
    unsigned char PINGRESP_RESP[] = { 0xD0, 0x0 };
    size_t length = sizeof(PINGRESP_RESP) / sizeof(PINGRESP_RESP[0]);

    TEST_COMPLETE_DATA_INSTANCE testData = { 0 };

    MQTTCODEC_HANDLE handle = mqtt_codec_create(TestOnCompleteCallback, &testData);

    umock_c_reset_all_calls();
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    g_curr_packet_type = PINGRESP_TYPE;

    // act
    for (size_t index = 0; index < length; index++)
    {
        // Send 1 byte at a time
        mqtt_codec_bytesReceived(handle, PINGRESP_RESP + index, 1);
    }

    // assert
    ASSERT_IS_TRUE(g_callbackInvoked);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    mqtt_codec_destroy(handle);
}

TEST_FUNCTION(mqtt_codec_bytesReceived_publish_long_message_succeed)
{
    // arrange
    size_t i;
    g_curr_packet_type = PUBLISH_TYPE;

    unsigned char PUBLISH[] = {
        0x32,  0xdc,  0x01,  0x00,  0xd5,  0x64,  0x65,  0x76,  0x69,  0x63,  0x65,  0x73,  0x2f,  0x74,  0x65,  0x73,  0x74,  0x44,  0x65,  0x76,  0x69,
        0x63,  0x65,  0x2f,  0x6d,  0x65,  0x73,  0x73,  0x61,  0x67,  0x65,  0x73,  0x2f,  0x64,  0x65,  0x76,  0x69,  0x63,  0x65,  0x62,  0x6f,
        0x75,  0x6e,  0x64,  0x2f,  0x69,  0x6f,  0x74,  0x68,  0x75,  0x62,  0x2d,  0x61,  0x63,  0x6b,  0x3d,  0x66,  0x75,  0x6c,  0x6c,  0x26,
        0x25,  0x32,  0x34,  0x2e,  0x6d,  0x69,  0x64,  0x3d,  0x37,  0x35,  0x63,  0x65,  0x63,  0x30,  0x62,  0x32,  0x2d,  0x66,  0x30,  0x36,
        0x39,  0x2d,  0x34,  0x66,  0x32,  0x61,  0x2d,  0x39,  0x38,  0x39,  0x35,  0x2d,  0x66,  0x31,  0x62,  0x35,  0x30,  0x34,  0x36,  0x62,
        0x31,  0x34,  0x35,  0x63,  0x26,  0x25,  0x32,  0x34,  0x2e,  0x74,  0x6f,  0x3d,  0x25,  0x32,  0x46,  0x64,  0x65,  0x76,  0x69,  0x63,
        0x65,  0x73,  0x25,  0x32,  0x46,  0x74,  0x65,  0x73,  0x74,  0x44,  0x65,  0x76,  0x69,  0x63,  0x65,  0x25,  0x32,  0x46,  0x6d,  0x65,
        0x73,  0x73,  0x61,  0x67,  0x65,  0x73,  0x25,  0x32,  0x46,  0x64,  0x65,  0x76,  0x69,  0x63,  0x65,  0x42,  0x6f,  0x75,  0x6e,  0x64,
        0x26,  0x25,  0x32,  0x34,  0x2e,  0x63,  0x69,  0x64,  0x26,  0x25,  0x32,  0x34,  0x2e,  0x75,  0x69,  0x64,  0x3d,  0x53,  0x79,  0x73,
        0x74,  0x65,  0x6d,  0x2e,  0x41,  0x72,  0x72,  0x61,  0x79,  0x53,  0x65,  0x67,  0x6d,  0x65,  0x6e,  0x74,  0x25,  0x36,  0x30,  0x31,
        0x25,  0x35,  0x42,  0x53,  0x79,  0x73,  0x74,  0x65,  0x6d,  0x2e,  0x42,  0x79,  0x74,  0x65,  0x25,  0x35,  0x44,  0x00,  0x0e,  0x4d,
        0x4c,  0x42
    };

    size_t length = sizeof(PUBLISH) / sizeof(PUBLISH[0]);
    TEST_COMPLETE_DATA_INSTANCE testData = { 0 };
    testData.dataHeader = PUBLISH + (FIXED_HEADER_SIZE + 1);
    testData.Length = length - (FIXED_HEADER_SIZE + 1);

    MQTTCODEC_HANDLE handle = mqtt_codec_create(TestOnCompleteCallback, &testData);

    umock_c_reset_all_calls();

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    for (i = 0; i < testData.Length; i++)
    {
        EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
        EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    }
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    for (size_t index = 0; index < length; index++)
    {
        // Send 1 byte at a time
        mqtt_codec_bytesReceived(handle, PUBLISH + index, 1);
    }

    // assert
    ASSERT_IS_TRUE(g_callbackInvoked);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    mqtt_codec_destroy(handle);
}

/* Codes_SRS_MQTT_CODEC_07_033: [mqtt_codec_bytesReceived constructs a sequence of bytes into the corresponding MQTT packets and on success returns zero.] */
/* Codes_SRS_MQTT_CODEC_07_034: [Upon a constructing a complete MQTT packet mqtt_codec_bytesReceived shall call the ON_PACKET_COMPLETE_CALLBACK function.] */
TEST_FUNCTION(mqtt_codec_bytesReceived_publish_succeed)
{
    // arrange
    size_t i;
    g_curr_packet_type = PUBLISH_TYPE;

    //                            1    2     3     4     T     o     p     i     c     10    11    d     a     t     a     sp    M     s     g
    unsigned char PUBLISH[] = { 0x3F, 0x11, 0x00, 0x06, 0x54, 0x6f, 0x70, 0x69, 0x63, 0x12, 0x34, 0x64, 0x61, 0x74, 0x61, 0x20, 0x4d, 0x73, 0x67 };
    size_t length = sizeof(PUBLISH) / sizeof(PUBLISH[0]);
    TEST_COMPLETE_DATA_INSTANCE testData = { 0 };
    testData.dataHeader = PUBLISH + FIXED_HEADER_SIZE;
    testData.Length = length - FIXED_HEADER_SIZE;

    MQTTCODEC_HANDLE handle = mqtt_codec_create(TestOnCompleteCallback, &testData);

    umock_c_reset_all_calls();

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    for (i = 0; i < testData.Length; i++)
    {
        EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
        EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    }
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    for (size_t index = 0; index < length; index++)
    {
        // Send 1 byte at a time
        mqtt_codec_bytesReceived(handle, PUBLISH + index, 1);
    }

    // assert
    ASSERT_IS_TRUE(g_callbackInvoked);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    mqtt_codec_destroy(handle);
}

/* Codes_SRS_MQTT_CODEC_07_033: [mqtt_codec_bytesReceived constructs a sequence of bytes into the corresponding MQTT packets and on success returns zero.] */
/* Codes_SRS_MQTT_CODEC_07_034: [Upon a constructing a complete MQTT packet mqtt_codec_bytesReceived shall call the ON_PACKET_COMPLETE_CALLBACK function.] */
TEST_FUNCTION(mqtt_codec_bytesReceived_publish_second_succeed)
{
    // arrange
    size_t i;
    g_curr_packet_type = PUBLISH_TYPE;

    //                            1    2     3     4     T     o     p     i     c     10    11    d     a     t     a     sp    M     s     g
    unsigned char PUBLISH[] = { 0x30, 0x1e, 0x00, 0x04, 0x6d, 0x73, 0x67, 0x41, 0x00, 0x16, 0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x61, 0x70, 0x70, 0x20, 0x6d, 0x73, 0x67, 0x20, 0x41, 0x2e };
    //unsigned char PUBLISH[] = { 0x3F, 0x11, 0x00, 0x06, 0x54, 0x6f, 0x70, 0x69, 0x63, 0x12, 0x34, 0x64, 0x61, 0x74, 0x61, 0x20, 0x4d, 0x73, 0x67 };
    size_t length = sizeof(PUBLISH) / sizeof(PUBLISH[0]);
    TEST_COMPLETE_DATA_INSTANCE testData = { 0 };
    testData.dataHeader = PUBLISH + FIXED_HEADER_SIZE;
    testData.Length = length - FIXED_HEADER_SIZE;

    MQTTCODEC_HANDLE handle = mqtt_codec_create(TestOnCompleteCallback, &testData);

    umock_c_reset_all_calls();

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    for (i = 0; i < testData.Length; i++)
    {
        EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
        EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    }
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    for (size_t index = 0; index < length; index++)
    {
        // Send 1 byte at a time
        mqtt_codec_bytesReceived(handle, PUBLISH + index, 1);
    }

    // assert
    ASSERT_IS_TRUE(g_callbackInvoked);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    mqtt_codec_destroy(handle);
}

/* Codes_SRS_MQTT_CODEC_07_033: [mqtt_codec_bytesReceived constructs a sequence of bytes into the corresponding MQTT packets and on success returns zero.] */
/* Codes_SRS_MQTT_CODEC_07_034: [Upon a constructing a complete MQTT packet mqtt_codec_bytesReceived shall call the ON_PACKET_COMPLETE_CALLBACK function.] */
TEST_FUNCTION(mqtt_codec_bytesReceived_suback_succeed)
{
    // arrange
    size_t i;
    g_curr_packet_type = SUBACK_TYPE;

    unsigned char SUBACK_RESP[] = { 0x90, 0x5, 0x12, 0x34, 0x01, 0x80, 0x02 };
    size_t length = sizeof(SUBACK_RESP) / sizeof(SUBACK_RESP[0]);

    TEST_COMPLETE_DATA_INSTANCE testData = { 0 };
    testData.dataHeader = SUBACK_RESP + FIXED_HEADER_SIZE;
    testData.Length = length - FIXED_HEADER_SIZE;
    MQTTCODEC_HANDLE handle = mqtt_codec_create(TestOnCompleteCallback, &testData);

    umock_c_reset_all_calls();

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    for (i = 0; i < testData.Length; i++)
    {
        EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
        EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    }
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    for (size_t index = 0; index < length; index++)
    {
        // Send 1 byte at a time
        mqtt_codec_bytesReceived(handle, SUBACK_RESP + index, 1);
    }

    // assert
    ASSERT_IS_TRUE(g_callbackInvoked);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    mqtt_codec_destroy(handle);
}

/* Codes_SRS_MQTT_CODEC_07_033: [mqtt_codec_bytesReceived constructs a sequence of bytes into the corresponding MQTT packets and on success returns zero.] */
/* Codes_SRS_MQTT_CODEC_07_034: [Upon a constructing a complete MQTT packet mqtt_codec_bytesReceived shall call the ON_PACKET_COMPLETE_CALLBACK function.] */
TEST_FUNCTION(mqtt_codec_bytesReceived_unsuback_succeed)
{
    // arrange
    size_t i;
    g_curr_packet_type = UNSUBACK_TYPE;

    unsigned char UNSUBACK_RESP[] = { 0xB0, 0x5, 0x12, 0x34, 0x01, 0x80, 0x02 };
    size_t length = sizeof(UNSUBACK_RESP) / sizeof(UNSUBACK_RESP[0]);

    TEST_COMPLETE_DATA_INSTANCE testData = { 0 };
    testData.dataHeader = UNSUBACK_RESP + FIXED_HEADER_SIZE;
    testData.Length = length - FIXED_HEADER_SIZE;
    MQTTCODEC_HANDLE handle = mqtt_codec_create(TestOnCompleteCallback, &testData);

    umock_c_reset_all_calls();

    EXPECTED_CALL(BUFFER_new());
    EXPECTED_CALL(BUFFER_pre_build(IGNORED_PTR_ARG, IGNORED_NUM_ARG));
    for (i = 0; i < testData.Length; i++)
    {
        EXPECTED_CALL(BUFFER_u_char(IGNORED_PTR_ARG));
        EXPECTED_CALL(BUFFER_length(IGNORED_PTR_ARG));
    }
    EXPECTED_CALL(BUFFER_delete(IGNORED_PTR_ARG));

    // act
    for (size_t index = 0; index < length; index++)
    {
        // Send 1 byte at a time
        mqtt_codec_bytesReceived(handle, UNSUBACK_RESP + index, 1);
    }

    // assert
    ASSERT_IS_TRUE(g_callbackInvoked);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    // cleanup
    mqtt_codec_destroy(handle);
}

END_TEST_SUITE(mqtt_codec_ut)
