#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <exception>

#include <fastdds/dds/core/status/SubscriptionMatchedStatus.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastrtps/attributes/ParticipantAttributes.h>
#include <fastrtps/attributes/SubscriberAttributes.h>
#include <fastrtps/subscriber/SampleInfo.h>
#include <fastrtps/transport/UDPv4TransportDescriptor.h>

#include "TestMsgPubSubTypes.h"

using namespace eprosima::fastdds::dds;
using namespace eprosima::fastrtps::rtps;

std::mutex g_cond_mutex;
std::condition_variable g_cond;
std::atomic_bool g_request_exit{false};

std::atomic_uint32_t g_connect_count;

#define SUBSCRIBER_NUM_PER_PARTICIPANT 10

class TestSubscriber {
public:
  TestSubscriber(std::string participant_name, std::string topic_name_base,
                 uint32_t topic_index)
      : participant_name_(participant_name),
        topic_name_base_(topic_name_base),
        topic_index_(topic_index),
        participant_(nullptr),
        subscribers_({}),
        topics_({}),
        readers_({}),
        type_(new TestMsgPubSubType()) {
  }

  virtual ~TestSubscriber(){

    uint32_t index = 0;
    for (auto sub: subscribers_) {
      if (sub != nullptr) {
        if (readers_[index] != nullptr) {
          sub->delete_datareader(readers_[index]);
        }
        participant_->delete_subscriber(sub);
      }
    }

    for (auto topic : topics_) {
      if (topic != nullptr)
      {
        participant_->delete_topic(topic);
      }
    }
    DomainParticipantFactory::get_instance()->delete_participant(participant_);
  }

  bool init(bool use_env)
  {
    DomainParticipantQos pqos = PARTICIPANT_QOS_DEFAULT;
    pqos.name(participant_name_);
    auto factory = DomainParticipantFactory::get_instance();

    if (use_env)
    {
        factory->load_profiles();
        factory->get_default_participant_qos(pqos);
    }

    #if 0
    // Create a descriptor for the new transport.
    auto udp_transport = std::make_shared<UDPv4TransportDescriptor>();
    udp_transport->sendBufferSize = 9216;
    udp_transport->receiveBufferSize = 9216;
    udp_transport->maxMessageSize = 9216;
    udp_transport->non_blocking_send = true;

    // Link the Transport Layer to the Participant.
    pqos.transport().user_transports.push_back(udp_transport);

    // Avoid using the default transport
    pqos.transport().use_builtin_transports = false;
    #endif

    participant_ = factory->create_participant(6, pqos);

    if (participant_ == nullptr)
    {
        return false;
    }

    //REGISTER THE TYPE
    type_.register_type(participant_);

    // get the subscriber QOS
    SubscriberQos sqos = SUBSCRIBER_QOS_DEFAULT;

    if (use_env) {
        participant_->get_default_subscriber_qos(sqos);
    }

    // get the topic QOS
    TopicQos tqos = TOPIC_QOS_DEFAULT;

    if (use_env) {
        participant_->get_default_topic_qos(tqos);
    }

    for(int i = 0; i < SUBSCRIBER_NUM_PER_PARTICIPANT; i++) {
      auto sub = participant_->create_subscriber(sqos, nullptr);
      if (sub == nullptr) {
        throw std::runtime_error("Create subscriber failed !");
      }

      subscribers_.emplace_back(sub);

      auto cur_topic_index = topic_index_ + i;
      auto cur_topic_name = topic_name_base_ + std::to_string(cur_topic_index);

      auto topic = participant_->create_topic(
        cur_topic_name,
        "TestMsg",
        tqos);
      if (topic == nullptr) {
        throw std::runtime_error("Create topic failed !");
      }
      topics_.emplace_back(topic);

      auto listener = std::make_shared<SubListener>();
      listener->topic_name_ = cur_topic_name;
      listener->participant_name_ = participant_name_;
      sub_listeners_.emplace_back(listener);

      DataReaderQos rqos = DATAREADER_QOS_DEFAULT;
      rqos.reliability().kind = RELIABLE_RELIABILITY_QOS;

      if (use_env) {
        sub->get_default_datareader_qos(rqos);
      }

      auto reader = sub->create_datareader(topic, rqos, listener.get());
      if (reader == nullptr) {
        throw std::runtime_error("Create reader failed !");
      }
      readers_.emplace_back(reader);
    }

    return true;
  }

private:
  std::string participant_name_;
  std::string topic_name_base_;
  uint32_t topic_index_;

  eprosima::fastdds::dds::DomainParticipant* participant_;

  std::vector<eprosima::fastdds::dds::Subscriber *> subscribers_;

  std::vector<eprosima::fastdds::dds::Topic *> topics_;

  std::vector<eprosima::fastdds::dds::DataReader *> readers_;

  eprosima::fastdds::dds::TypeSupport type_;

  class SubListener : public eprosima::fastdds::dds::DataReaderListener {
  public:
    SubListener() {}

    ~SubListener() override {}

    void on_data_available(eprosima::fastdds::dds::DataReader *reader) override {
      return;
    }

    void on_subscription_matched(
        eprosima::fastdds::dds::DataReader *reader,
        const eprosima::fastdds::dds::SubscriptionMatchedStatus &info) override
    {
        if (info.current_count_change == 1) {
          std::cout << participant_name_ << " connect to " << topic_name_ << std::endl;
          ++g_connect_count;
        } else if (info.current_count_change == -1) {
          std::cout << "Subscriber unmatched." << std::endl;
          --g_connect_count;
        } else {
          std::cout << info.current_count_change
                    << " is not a valid value for SubscriptionMatchedStatus "
                       "current count change"
                    << std::endl;
        }
    }

    std::string participant_name_;
    std::string topic_name_;
  };

  std::vector<std::shared_ptr<SubListener>> sub_listeners_;
};

void signal_handler(int signal)
{
  g_request_exit = true;
  g_cond.notify_all();
}

int main(int argc, char **argv)
{
  if (argc != 2) {
    std::printf("Usage: %s Participant_Num\n", argv[0]);
    return EXIT_FAILURE;
  }

  // Install signal handler
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  int participant_num = std::stoi(argv[1]);

  std::vector<std::shared_ptr<TestSubscriber>> sub_list;

  const std::string participation_name_base = "sub_participation_";
  const std::string topic_name_base = "topic_";

  const uint32_t total_datareader = participant_num * SUBSCRIBER_NUM_PER_PARTICIPANT;
  uint32_t topic_index = 1;

  for (int i=1; i <= participant_num; i++) {
    std::string participation_name = participation_name_base + std::to_string(i);
    auto sub = std::make_shared<TestSubscriber>(participation_name, topic_name_base, topic_index);
    sub->init(false);
    sub_list.emplace_back(sub);
    topic_index += SUBSCRIBER_NUM_PER_PARTICIPANT;
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }

  while (!g_request_exit) {
    if (g_connect_count != total_datareader) {
      std::this_thread::sleep_for(std::chrono::microseconds(300));
    } else {
      std::cout << "+++ All subscriptions connect publishers ! +++" << std::endl;
      break;
    }
  }

  if(!g_request_exit) {
    std::unique_lock<std::mutex> lock(g_cond_mutex);
    g_cond.wait(lock, []{
      return g_request_exit == true;
    });
  }

  sub_list.clear();

  return EXIT_SUCCESS;
}
