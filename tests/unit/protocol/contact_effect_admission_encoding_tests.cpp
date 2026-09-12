#include "fixtures/contact_effect_admission_fixture.hpp"

#include "components/contact_effect_admission_component_encoding.hpp"
#include "protocol_v3_json_encoding.hpp"

#include <boost/json/parse.hpp>
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace protocol = blob_royale::protocol;
namespace simulation = blob_royale::simulation;
namespace fixture = protocol::contact_effect_admission_fixture;
namespace v3 = protocol::v3_test_fixture;

namespace {

class AdmissionSink final : public protocol::ComponentObjectSink {
public:
  std::vector<std::pair<std::string, std::string>> members;
  void set_string(std::string_view name, std::string_view value) override {
    members.emplace_back(name, value);
  }
  void set_number(std::string_view, double) override { unexpected(); }
  void set_unsigned(std::string_view, std::uint64_t) override { unexpected(); }
  void set_signed(std::string_view, std::int64_t) override { unexpected(); }
  void set_boolean(std::string_view, bool) override { unexpected(); }
  void set_vector(std::string_view, double, double) override { unexpected(); }
  void set_object_array(
      std::string_view, std::size_t,
      const std::function<void(std::size_t, protocol::ComponentObjectSink&)>&) override {
    unexpected();
  }

private:
  [[noreturn]] static void unexpected() {
    throw std::logic_error("TEST.ADMISSION_NONSTRING_MEMBER");
  }
};

std::string encode(const simulation::WorldSnapshot& snapshot) {
  return protocol::encode_snapshot_message_v3(snapshot, v3::StubControllerDirectory{}, std::nullopt,
                                              v3::session_request_id(),
                                              v3::kSnapshotMessageSequence, v3::kSnapshotTimestamp);
}

} // namespace

TEST_CASE("contact effect admission is public sparse state with the exact v3 component example") {
  const auto snapshot = fixture::snapshot(true);
  REQUIRE(snapshot.components<simulation::ContactEffectAdmission>().size() == 1);
  REQUIRE(snapshot.components<simulation::ContactEffectAdmission>().front().value.policy ==
          simulation::ContactEffectPolicy::kAnyTouch);
  const auto encoded = encode(snapshot);
  const auto document = boost::json::parse(encoded);
  const auto& components = document.as_object()
                               .at("data")
                               .as_object()
                               .at("entities")
                               .as_array()
                               .front()
                               .as_object()
                               .at("components")
                               .as_object();
  REQUIRE(
      components.at("contact_effect_admission") ==
      boost::json::parse(v3::read_v3_golden_example("contact-effect-admission-component.json")));
  REQUIRE(encoded.find(R"("contact_effect_admission":{"policy":"any_touch"},"controllable":)") !=
          std::string::npos);
}

TEST_CASE("closing impact publishes no redundant contact effect admission component") {
  const auto snapshot = fixture::snapshot(false);
  REQUIRE(snapshot.components<simulation::ContactEffectAdmission>().empty());
  REQUIRE(encode(snapshot).find("contact_effect_admission") == std::string::npos);
}

TEST_CASE("contact effect admission encoder rejects malformed sparse values before writing") {
  const protocol::ComponentEncodingContext context{simulation::EntityId::create(fixture::kEntity),
                                                   nullptr};
  for (const auto policy : fixture::kInvalidStoredPolicies) {
    AdmissionSink sink;
    try {
      protocol::ComponentWireEncoding<simulation::ContactEffectAdmission>::encode(
          simulation::ContactEffectAdmission{policy}, context, sink);
      FAIL("invalid sparse policy reached the encoder");
    } catch (const protocol::ProtocolEncodingError& error) {
      REQUIRE(error.error_code() == protocol::ProtocolEncodingErrorCode::kComponentValueOutOfRange);
      REQUIRE(error.context() == "contact_effect_admission.policy");
    }
    REQUIRE(sink.members.empty());
  }
}
