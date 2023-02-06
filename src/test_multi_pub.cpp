#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastrtps/attributes/ParticipantAttributes.h>
#include <fastrtps/attributes/PublisherAttributes.h>
#include <fastrtps/transport/UDPv4TransportDescriptor.h>

#include "TestMsgPubSubTypes.h"

using namespace eprosima::fastdds::dds;
using namespace eprosima::fastrtps::rtps;

std::mutex g_cond_mutex;
std::condition_variable g_cond;
std::atomic_bool g_request_exit{false};

#define PUBLISHER_NUM_PER_PARTICIPANT 10

class TestPublisher {
public:
  TestPublisher(std::string participant_name, std::string topic_name_base, uint32_t topic_index)
  : participant_name_(participant_name),
    topic_name_base_(topic_name_base),
    topic_index_(topic_index),
    participant_(nullptr),
    publishers_({}),
    topics_({}),
    writers_({}),
    stop_(false),
    type_(new TestMsgPubSubType()) {}

  virtual ~TestPublisher() {

    int index = 0;
    for(auto pub: publishers_) {
      if (pub != nullptr) {
        if (writers_[index] != nullptr) {
          pub->delete_datawriter(writers_[index]);
        }
        index++;
        participant_->delete_publisher(pub);
      }
    }

    for(auto topic: topics_) {
      participant_->delete_topic(topic);
    }

    DomainParticipantFactory::get_instance()->delete_participant(participant_);

    if(thread_.joinable()) {
      thread_.join();
    }
  }

  //! Initialize
  bool init(bool use_env) {
    DomainParticipantQos pqos = PARTICIPANT_QOS_DEFAULT;
    pqos.name(participant_name_);
    auto factory = DomainParticipantFactory::get_instance();

    if (use_env) {
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

    if (participant_ == nullptr) {
      return false;
    }

    // Register the type
    type_.register_type(participant_);

    // Get the publisher QOS
    PublisherQos pubqos = PUBLISHER_QOS_DEFAULT;

    if (use_env) {
      participant_->get_default_publisher_qos(pubqos);
    }

    // Get the topic QOS
    TopicQos tqos = TOPIC_QOS_DEFAULT;

    if (use_env) {
      participant_->get_default_topic_qos(tqos);
    }

    for(int i = 0; i < PUBLISHER_NUM_PER_PARTICIPANT; i++) {
      auto pub = participant_->create_publisher(
        pubqos,
        nullptr);

      if (pub == nullptr) {
        throw std::runtime_error("Create publisher failed !");
      }

      publishers_.emplace_back(pub);

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

      // Get the datawriter QOS
      DataWriterQos wqos = DATAWRITER_QOS_DEFAULT;

      if (use_env) {
        pub->get_default_datawriter_qos(wqos);
      }

      auto writer = pub->create_datawriter(
        topic,
        wqos,
        &listener_);

      if (writer == nullptr) {
        throw std::runtime_error("Create datawriter failed !");
      }

      writers_.emplace_back(writer);
    }

    return true;
  }

  void run() {
    auto thread_process = [this]() {
      std::unique_lock<std::mutex> lock(cond_mutex_);
      cond_.wait(lock, [this]{
        return this->stop_ == true;
      });
    };

    thread_ = std::thread(thread_process);
  }

  void notify_exit() {
    stop_ = true;
    cond_.notify_one();
  }

private:
  std::string participant_name_;
  std::string topic_name_base_;
  uint32_t topic_index_;

  TestMsg msg_;

  eprosima::fastdds::dds::DomainParticipant *participant_;

  std::vector<eprosima::fastdds::dds::Publisher *> publishers_;

  std::vector<eprosima::fastdds::dds::Topic *> topics_;

  std::vector<eprosima::fastdds::dds::DataWriter *> writers_;

  std::atomic_bool stop_;

  class PubListener : public eprosima::fastdds::dds::DataWriterListener {
  public:
    PubListener() {}

    ~PubListener() override {}

    void on_publication_matched(
        eprosima::fastdds::dds::DataWriter *writer,
        const eprosima::fastdds::dds::PublicationMatchedStatus &info) override {
      if (info.current_count_change == 1) {
        std::cout << "A subscription is connected !" << std::endl;
      } else if (info.current_count_change == -1) {
        std::cout << "A subscription is disconnected !" << std::endl;
      } else {
        std::cout << info.current_count_change
                  << " is not a valid value for PublicationMatchedStatus "
                     "current count change"
                  << std::endl;
      }
    }
  } listener_;

  eprosima::fastdds::dds::TypeSupport type_;

  std::thread thread_;

  std::mutex cond_mutex_;
  std::condition_variable cond_;
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

  int participant_num = std::stoi(argv[1]);

  // Install signal handler
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  const std::string participation_name_base = "pub_participation_";
  const std::string topic_name_base = "topic_";
  uint32_t topic_index = 1;
  std::vector<std::shared_ptr<TestPublisher>> pub_list;
  for(int i = 0; i < participant_num; i++) {
    auto pub = std::make_shared<TestPublisher>(
        participation_name_base + std::to_string(i + 1),
        topic_name_base,
        topic_index);
    pub->init(false);
    pub->run();
    pub_list.emplace_back(pub);
    topic_index += PUBLISHER_NUM_PER_PARTICIPANT;
  }

  if(!g_request_exit) {
    std::unique_lock<std::mutex> lock(g_cond_mutex);
    g_cond.wait(lock, []{
      return g_request_exit == true;
    });
  }

  for (auto & pub: pub_list) {
    pub->notify_exit();
  }

  pub_list.clear();

  return EXIT_SUCCESS;
}
