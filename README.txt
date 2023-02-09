To launch this test open two different consoles:

In the first one launch: ./test_multi_pub participant_num
In the second one: ./test_multi_sub participant_num

In order to use xml profiles:
    - reference the xml profiles file setting the environment variable FASTRTPS_DEFAULT_PROFILES_FILE to its path.
    - name it DEFAULT_FASTRTPS_PROFILES.xml and make sure the file is besides the DDSHelloWorldExample binary.

e.g.
$ FASTRTPS_DEFAULT_PROFILES_FILE=./conf/DEFAULT_FASTRTPS_PROFILES.xml build/test_multi_pub 10
# or
$ FASTRTPS_DEFAULT_PROFILES_FILE=./conf/DEFAULT_FASTRTPS_PROFILES.xml build/test_multi_sub 10



