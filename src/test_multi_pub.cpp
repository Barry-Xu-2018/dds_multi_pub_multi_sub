#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
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

#include "TestMsgPubSubTypes.h"

using namespace eprosima::fastdds::dds;

std::mutex g_cond_mutex;
std::condition_variable g_cond;
std::atomic_bool g_request_exit{false};

class TestPublisher {
public:
  TestPublisher(std::string participant_name, std::string topic_name)
  : participant_name_(participant_name),
    topic_name_(topic_name),
    participant_(nullptr),
    publisher_(nullptr),
    topic_(nullptr),
    writer_(nullptr),
    stop_(false),
    type_(new TestMsgPubSubType()) {}

  virtual ~TestPublisher() {
    if (writer_ != nullptr)
    {
        publisher_->delete_datawriter(writer_);
    }
    if (publisher_ != nullptr)
    {
        participant_->delete_publisher(publisher_);
    }
    if (topic_ != nullptr)
    {
        participant_->delete_topic(topic_);
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

    //CREATE THE PUBLISHER
    PublisherQos pubqos = PUBLISHER_QOS_DEFAULT;

    if (use_env)
    {
        participant_->get_default_publisher_qos(pubqos);
    }

    publisher_ = participant_->create_publisher(
        pubqos,
        nullptr);

    if (publisher_ == nullptr)
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

    // CREATE THE WRITER
    DataWriterQos wqos = DATAWRITER_QOS_DEFAULT;

    if (use_env)
    {
        publisher_->get_default_datawriter_qos(wqos);
    }

    writer_ = publisher_->create_datawriter(
        topic_,
        wqos,
        &listener_);

    if (writer_ == nullptr)
    {
        return false;
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
  std::string topic_name_;

  TestMsg msg_;

  eprosima::fastdds::dds::DomainParticipant *participant_;

  eprosima::fastdds::dds::Publisher *publisher_;

  eprosima::fastdds::dds::Topic *topic_;

  eprosima::fastdds::dds::DataWriter *writer_;

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
    std::printf("Usage: %s Topic_Num", argv[0]);
    return EXIT_FAILURE;
  }

  int topic_num = std::stoi(argv[1]);

  // Install signal handler
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  const std::string participation_name_base = "pub_participation_";
  const std::string topic_name_base = "topic_";
  std::vector<std::shared_ptr<TestPublisher>> pub_list;
  for(int i; i < topic_num; i++) {
    auto pub = std::make_shared<TestPublisher>(
        participation_name_base + std::to_string(i + 1),
        topic_name_base + std::to_string(i + 1));
    pub->init(false);
    pub->run();
    pub_list.emplace_back(pub);
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