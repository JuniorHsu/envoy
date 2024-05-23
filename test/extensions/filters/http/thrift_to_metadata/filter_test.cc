#include <memory>

#include "envoy/http/header_map.h"

#include "source/extensions/filters/http/thrift_to_metadata/filter.h"
#include "source/extensions/filters/network/thrift_proxy/protocol_converter.h"

#include "test/common/stream_info/test_util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ThriftToMetadata {

MATCHER_P(MapEq, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_EQ(obj.fields().at(entry.first).string_value(), entry.second);
  }
  return true;
}

MATCHER_P(MapNumEq, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_EQ(obj.fields().at(entry.first).number_value(), entry.second);
  }
  return true;
}
// class FilterTest : public testing::Test {
class FilterTest : public testing::TestWithParam<std::tuple<TransportType, ProtocolType>> {
public:
  FilterTest() = default;

  const std::string config_yaml_ = R"EOF(
request_rules:
- field: PROTOCOL
  on_present:
    metadata_namespace: envoy.lb
    key: protocol
  on_missing:
    metadata_namespace: envoy.lb
    key: protocol
    value: "unknown"
- field: TRANSPORT
  on_present:
    metadata_namespace: envoy.lb
    key: transport
  on_missing:
    metadata_namespace: envoy.lb
    key: transport
    value: "unknown"
- field: MESSAGE_TYPE
  on_present:
    metadata_namespace: envoy.lb
    key: request_message_type
  on_missing:
    metadata_namespace: envoy.lb
    key: request_message_type
    value: "unknown"
- field: METHOD_NAME
  on_present:
    metadata_namespace: envoy.lb
    key: method_name
  on_missing:
    metadata_namespace: envoy.lb
    key: method_name
    value: "unknown"
response_rules:
- field: MESSAGE_TYPE
  on_present:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_message_type
  on_missing:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_message_type
    value: "exception"
- field: REPLY_TYPE
  on_present:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_reply_type
  on_missing:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_reply_type
    value: "error"
- field: PROTOCOL
  on_present:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: protocol
  on_missing:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: protocol
    value: "unknown"
- field: TRANSPORT
  on_present:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: transport
  on_missing:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: transport
    value: "unknown"
- field: METHOD_NAME
  on_present:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: method_name
  on_missing:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: method_name
    value: "unknown"
)EOF";

  void initializeFilter(const std::string& yaml) {
    envoy::extensions::filters::http::thrift_to_metadata::v3::ThriftToMetadata config;
    TestUtility::loadFromYaml(yaml, config);
    config_ = std::make_shared<FilterConfig>(config, *scope_.rootScope());
    filter_ = std::make_shared<Filter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  uint64_t getCounterValue(const std::string& name) {
    const auto counter = TestUtility::findCounter(scope_, name);
    return counter != nullptr ? counter->value() : 0;
  }

  void testRequest(NetworkFilters::ThriftProxy::TransportType transport_type,
                   NetworkFilters::ThriftProxy::ProtocolType protocol_type,
                   NetworkFilters::ThriftProxy::MessageType msg_type,
                   const std::map<std::string, std::string>& expected_metadata) {
    initializeFilter(config_yaml_);
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, false));
    EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
    EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected_metadata)));

    Buffer::OwnedImpl buffer;
    writeMessage(buffer, transport_type, protocol_type, msg_type);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));

    EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 1);
    EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
    EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
    EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 0);
  }

  void testResponse(NetworkFilters::ThriftProxy::TransportType transport_type,
                    NetworkFilters::ThriftProxy::ProtocolType protocol_type,
                    NetworkFilters::ThriftProxy::MessageType msg_type,
                    NetworkFilters::ThriftProxy::ReplyType reply_type,
                    const std::map<std::string, std::string>& expected_metadata) {
    UNREFERENCED_PARAMETER(reply_type);

    initializeFilter(config_yaml_);
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->encodeHeaders(response_headers_, false));
    EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
    EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.filters.http.thrift_to_metadata",
                                                 MapEq(expected_metadata)));

    Buffer::OwnedImpl buffer;
    writeMessage(buffer, transport_type, protocol_type, msg_type, reply_type);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

    EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 1);
    EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 0);
    EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
    EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 0);
  }

  void
  writeMessage(Buffer::OwnedImpl& buffer, NetworkFilters::ThriftProxy::TransportType transport_type,
               NetworkFilters::ThriftProxy::ProtocolType protocol_type,
               NetworkFilters::ThriftProxy::MessageType msg_type,
               absl::optional<NetworkFilters::ThriftProxy::ReplyType> reply_type = absl::nullopt) {
    Buffer::OwnedImpl request_buffer;
    ProtocolConverterSharedPtr protocol_converter = std::make_shared<ProtocolConverter>();
    ProtocolPtr protocol = createProtocol(protocol_type);
    protocol_converter->initProtocolConverter(*protocol, request_buffer);

    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    metadata->setProtocol(protocol_type);
    metadata->setMethodName(method_name_);
    metadata->setMessageType(msg_type);
    metadata->setSequenceId(1234);

    protocol_converter->messageBegin(metadata);
    protocol_converter->structBegin("");
    int16_t field_id = 0;
    FieldType field_type_string = FieldType::String;
    FieldType field_type_struct = FieldType::Struct;
    FieldType field_type_stop = FieldType::Stop;
    if (msg_type == MessageType::Reply || msg_type == MessageType::Exception) {
      if (reply_type.value() == ReplyType::Success) {
        field_id = 0;
        protocol_converter->fieldBegin("", field_type_string, field_id);
        protocol_converter->stringValue("value1");
        protocol_converter->fieldEnd();
      } else {
        // successful response struct in field id 0, error (IDL exception) in field id greater than
        // 0
        field_id = 2;
        protocol_converter->fieldBegin("", field_type_struct, field_id);
        protocol_converter->structBegin("");
        field_id = 1;
        protocol_converter->fieldBegin("", field_type_string, field_id);
        protocol_converter->stringValue("err");
        protocol_converter->fieldEnd();
        field_id = 0;
        protocol_converter->fieldBegin("", field_type_stop, field_id);
        protocol_converter->structEnd();
        protocol_converter->fieldEnd();
      }
    }
    field_id = 0;
    protocol_converter->fieldBegin("", field_type_stop, field_id);
    protocol_converter->structEnd();
    protocol_converter->messageEnd();

    TransportPtr transport = createTransport(transport_type);
    transport->encodeFrame(buffer, *metadata, request_buffer);
  }

  TransportPtr createTransport(NetworkFilters::ThriftProxy::TransportType transport) {
    return NamedTransportConfigFactory::getFactory(transport).createTransport();
  }

  ProtocolPtr createProtocol(NetworkFilters::ThriftProxy::ProtocolType protocol) {
    return NamedProtocolConfigFactory::getFactory(protocol).createProtocol();
  }
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  envoy::config::core::v3::Metadata dynamic_metadata_;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<Filter> filter_;
  const std::string method_name_{"foo"};
  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/Service"}, {":method", "POST"}, {"Content-Type", "application/x-thrift"}};
  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"},
                                                    {"Content-Type", "application/x-thrift"}};
};

INSTANTIATE_TEST_SUITE_P(
    RequestCombinations, FilterTest,
    testing::Combine(testing::Values(TransportType::Unframed, TransportType::Framed,
                                     TransportType::Header),
                     testing::Values(ProtocolType::Binary, ProtocolType::Compact)));

TEST_P(FilterTest, CallRequestSuccess) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType msg_type = MessageType::Call;
  testRequest(transport_type, protocol_type, msg_type,
              {{"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
               {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
               {"request_message_type", MessageTypeNames::get().fromType(msg_type)},
               {"method_name", method_name_}});
}

TEST_P(FilterTest, OnewayRequestSuccess) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType msg_type = MessageType::Oneway;
  testRequest(transport_type, protocol_type, msg_type,
              {{"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
               {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
               {"request_message_type", MessageTypeNames::get().fromType(msg_type)},
               {"method_name", method_name_}});
}

TEST_P(FilterTest, ReplyResponseSuccess) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType msg_type = MessageType::Reply;
  testResponse(transport_type, protocol_type, msg_type, ReplyType::Success,
               {{"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
                {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
                {"response_message_type", MessageTypeNames::get().fromType(msg_type)},
                {"method_name", method_name_},
                {"response_reply_type", "success"}});
}

TEST_P(FilterTest, ExceptionResponseSuccess) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType msg_type = MessageType::Exception;
  testResponse(transport_type, protocol_type, msg_type, ReplyType::Success,
               {{"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
                {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
                {"response_message_type", MessageTypeNames::get().fromType(msg_type)},
                {"method_name", method_name_},
                // Exception mutates the reply type to error
                {"response_reply_type", "error"}});
}

TEST_P(FilterTest, ReplyResponseError) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType msg_type = MessageType::Reply;
  testResponse(transport_type, protocol_type, msg_type, ReplyType::Error,
               {{"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
                {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
                {"response_message_type", MessageTypeNames::get().fromType(msg_type)},
                {"method_name", method_name_},
                {"response_reply_type", "error"}});
}

} // namespace ThriftToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
