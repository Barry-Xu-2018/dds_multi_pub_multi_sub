#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

#include "TestMsgPubSubTypes.h"

using namespace eprosima::fastdds::dds;

std::mutex g_cond_mutex;
std::condition_variable g_cond;
std::atomic_bool g_request_exit{false};

std::atomic_uint32_t g_connect_count;

class TestSubscriber {
public:
  TestSubscriber(std::string participant_name, std::string topic_name)
      : participant_name_(participant_name),
        topic_name_(topic_name),
        participant_(nullptr),
        subscriber_(nullptr),
        topic_(nullptr),
        reader_(nullptr),
        type_(new TestMsgPubSubType())
  {
    listener_.participant_name_ = participant_name_;
    listener_.topic_name_ = topic_name_;
  }

  virtual ~TestSubscriber(){
    if (reader_ != nullptr)
    {
        subscriber_->delete_datareader(reader_);
    }
    if (topic_ != nullptr)
    {
        participant_->delete_topic(topic_);
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

    //CREATE THE TOPIC
    TopicQos tqos = TOPIC_QOS_DEFAULT;

    if (use_env)
    {
        participant_->get_default_topic_qos(tqos);
    }

    topic_ = participant_->create_topic(
        topic_name_,
        "TestMsg",
        tqos);

    if (topic_ == nullptr)
    {
        return false;
    }

    // CREATE THE READER
    DataReaderQos rqos = DATAREADER_QOS_DEFAULT;
    rqos.reliability().kind = RELIABLE_RELIABILITY_QOS;

    if (use_env)
    {
        subscriber_->get_default_datareader_qos(rqos);
    }

    reader_ = subscriber_->create_datareader(topic_, rqos, &listener_);

    if (reader_ == nullptr)
    {
        return false;
    }

    return true;
  }

private:
  std::string participant_name_;
  std::string topic_name_;

  eprosima::fastdds::dds::DomainParticipant* participant_;

  eprosima::fastdds::dds::Subscriber* subscriber_;

  eprosima::fastdds::dds::Topic* topic_;

  eprosima::fastdds::dds::DataReader* reader_;

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
        } else {
          std::cout << info.current_count_change
                    << " is not a valid value for SubscriptionMatchedStatus "
                       "current count change"
                    << std::endl;
        }
    }

    std::string participant_name_;
    std::string topic_name_;
  } listener_;
};

void signal_handler(int signal)
{
  g_request_exit = true;
  g_cond.notify_all();
}

int main(int argc, char **argv)
{
  if (argc != 3) {
    std::printf("Usage: %s Topic_Num Sub_Num_For_One_Topic\n", argv[0]);
    return EXIT_FAILURE;
  }

  // Install signal handler
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  int topic_num = std::stoi(argv[1]);
  int sub_num_for_one_topic = std::stoi(argv[2]);

  // One subscriber in one participant
  int total_participant = topic_num * sub_num_for_one_topic;

  std::vector<std::shared_ptr<TestSubscriber>> sub_list;

  const std::string participation_name_base = "sub_participation_";
  const std::string topic_name_base = "topic_";

  for (int i=1; i <= total_participant; i++) {
    std::string topic_name = topic_name_base + std::to_string(int((i-1)/sub_num_for_one_topic)+1);
    std::string participation_name = participation_name_base + std::to_string(i) + "_" + topic_name;
    auto sub = std::make_shared<TestSubscriber>(participation_name, topic_name);
    sub->init(false);
    sub_list.emplace_back(sub);
  }

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