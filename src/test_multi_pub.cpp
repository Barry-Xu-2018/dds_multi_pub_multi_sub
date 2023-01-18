#include <cstdlib>
#include <iostream>

#include "TestMsgPubSubTypes.h"

#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastrtps/attributes/ParticipantAttributes.h>
#include <fastrtps/attributes/PublisherAttributes.h>
#include <string>

using namespace eprosima::fastdds::dds;

class HelloWorldPublisher {
public:
  HelloWorldPublisher(std::string participant_name, std::string topic_name)
  : participant_name_(participant_name),
    topic_name_(topic_name),
    participant_(nullptr),
    publisher_(nullptr),
    topic_(nullptr),
    writer_(nullptr),
    type_(new TestMsgPubSubType()) {}

  virtual ~HelloWorldPublisher() {
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

  }

  //! Initialize
  bool init(bool use_env) {
    DomainParticipantQos pqos = PARTICIPANT_QOS_DEFAULT;
    pqos.name("Participant_pub");
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
        "HelloWorldTopic",
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

  //! Publish a sample
  bool publish(bool waitForListener = true);

  //! Run for number samples
  void run(uint32_t number, uint32_t sleep);

private:
  std::string participant_name_;
  std::string topic_name_;

  TestMsg msg_;

  eprosima::fastdds::dds::DomainParticipant *participant_;

  eprosima::fastdds::dds::Publisher *publisher_;

  eprosima::fastdds::dds::Topic *topic_;

  eprosima::fastdds::dds::DataWriter *writer_;

  bool stop_;

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

  void runThread(uint32_t number, uint32_t sleep);

  eprosima::fastdds::dds::TypeSupport type_;
};

int main(int argc, char **argv)
{
  if (argc != 2) {
    std::printf("Usage: %s Topic_Num", argv[0]);
    return EXIT_FAILURE;
  }



  return 0;
}