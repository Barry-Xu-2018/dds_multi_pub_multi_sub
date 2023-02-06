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

class TestSubscriber {
public:
  TestSubscriber(std::string participant_name, std::string topic_name_base,
                 uint32_t topic_num = 1, uint32_t reader_num_for_one_topic = 1)
      : participant_name_(participant_name),
        topic_name_base_(topic_name_base),
        topic_num_(topic_num),
        readers_for_one_topic_(reader_num_for_one_topic),
        participant_(nullptr),
        subscriber_(nullptr),
        topics_({}),
        readers_({}),
        type_(new TestMsgPubSubType()) {
  }

  virtual ~TestSubscriber(){

    for (auto reader : readers_) {
      if (reader != nullptr)
      {
        subscriber_->delete_datareader(reader);
      }
    }
    for (auto topic : topics_) {
      if (topic != nullptr)
      {
        participant_->delete_topic(topic);
      }
    }
    if (subscriber_ != nullptr)
    {
        participant_->delete_subscriber(subscriber_);
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

    participant_ = factory->create_participant(0, pqos);

    if (participant_ == nullptr)
    {
        return false;
    }

    //REGISTER THE TYPE
    type_.register_type(participant_);

    //CREATE THE SUBSCRIBER
    SubscriberQos sqos = SUBSCRIBER_QOS_DEFAULT;

    if (use_env)
    {
        participant_->get_default_subscriber_qos(sqos);
    }

    subscriber_ = participant_->create_subscriber(sqos, nullptr);

    if (subscriber_ == nullptr)
    {
        return false;
    }

    // THE TOPIC CONF
    TopicQos tqos = TOPIC_QOS_DEFAULT;

    if (use_env)
    {
        participant_->get_default_topic_qos(tqos);
    }

    // THE READER CONF
    DataReaderQos rqos = DATAREADER_QOS_DEFAULT;
    rqos.reliability().kind = RELIABLE_RELIABILITY_QOS;

    if (use_env)
    {
        subscriber_->get_default_datareader_qos(rqos);
    }

    for (int i = 1; i <= topic_num_ ; i++) {
      //std::cout << "+++ topic name: " << topic_name_base_ + std::to_string(i) << std::endl;
      auto topic_name = topic_name_base_ + std::to_string(i);
      auto topic = participant_->create_topic(
        topic_name,
        "TestMsg",
        tqos);

      if (topic == nullptr)
      {
        throw std::runtime_error("Cannot create topic !");
      }
      topics_.emplace_back(topic);

      auto listener = std::make_shared<SubListener>();
      listener->topic_name_ = topic_name;
      listener->participant_name_ = participant_name_;

      sub_listeners_.emplace_back(listener);

      for (int j = 0; j < readers_for_one_topic_; j++) {
        auto reader = subscriber_->create_datareader(topic, rqos, listener.get());

        if (reader == nullptr)
        {
           throw std::runtime_error("Cannot create reader !");
        }
        readers_.emplace_back(reader);
      }
    }

    return true;
  }

private:
  std::string participant_name_;
  std::string topic_name_base_;
  uint32_t topic_num_;
  uint32_t readers_for_one_topic_;

  eprosima::fastdds::dds::DomainParticipant* participant_;

  eprosima::fastdds::dds::Subscriber* subscriber_;

  std::vector<eprosima::fastdds::dds::Topic*> topics_;

  std::vector<eprosima::fastdds::dds::DataReader*> readers_;

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
  if (argc != 3) {
    std::printf("Usage: %s Topic_Num Reader_Num_For_One_Topic\n", argv[0]);
    return EXIT_FAILURE;
  }

  // Install signal handler
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  int topic_num = std::stoi(argv[1]);
  int reader_num_for_one_topic = std::stoi(argv[2]);

  // One subscriber in one participant
  int total_participant = topic_num * reader_num_for_one_topic;

  std::vector<std::shared_ptr<TestSubscriber>> sub_list;

  const std::string participation_name_base = "sub_participation_";
  const std::string topic_name_base = "topic_";

#if 0
  for (int i=1; i <= total_participant; i++) {
    std::string topic_name = topic_name_base + std::to_string(int((i-1)/reader_num_for_one_topic)+1);
    std::string participation_name = participation_name_base + std::to_string(i) + "_" + topic_name;
    auto sub = std::make_shared<TestSubscriber>(participation_name, topic_name);
    sub->init(false);
    sub_list.emplace_back(sub);
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }
#else
  auto sub = std::make_shared<TestSubscriber>(participation_name_base + "1", topic_name_base, topic_num, reader_num_for_one_topic);
  sub->init(false);
  sub_list.emplace_back(sub);
  std::this_thread::sleep_for(std::chrono::microseconds(300));
#endif

  while (!g_request_exit) {
    if (g_connect_count != total_participant) {
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
