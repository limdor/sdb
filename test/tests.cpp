#include <signal.h>
#include <sys/types.h>

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <libsdb/error.hpp>
#include <libsdb/process.hpp>

using namespace sdb;

namespace {
bool process_exists(pid_t pid) {
  auto ret = kill(pid, 0);
  return ret != -1 and errno != ESRCH;
}
char get_process_status(pid_t pid) {
  std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
  std::string data;
  std::getline(stat, data);
  auto index_of_last_parenthesis = data.rfind(')');
  auto index_of_status_indicator = index_of_last_parenthesis + 2;
  return data[index_of_status_indicator];
}
}  // namespace

TEST_CASE("process::launch success", "[process]") {
  auto proc = process::launch("yes");
  REQUIRE(process_exists(proc->pid()));
}

TEST_CASE("process::launch no such program", "[process]") {
  REQUIRE_THROWS_AS(process::launch("not_existing_program"), error);
}

TEST_CASE(
    "process::get_registers read instruction pointer after process attachement",
    "[process]") {
  auto target = process::launch("targets/run_endlessly");
  auto& registers = target->get_registers();
  auto value = registers.read_by_id_as<std::uint64_t>(register_id::rip);
  // We don't know what the value of the instruction pointer will be, but it
  // should not be zero.
  REQUIRE(value != 0);
}

TEST_CASE("process::get_registers read x87 register after process attachement",
          "[process]") {
  auto target = process::launch("targets/run_endlessly");
  auto& registers = target->get_registers();
  auto value = registers.read_by_id_as<std::uint64_t>(register_id::st0);
}

TEST_CASE("process::get_registers write ah after process attachement",
          "[process]") {
  auto target = process::launch("targets/run_endlessly");
  auto& registers = target->get_registers();
  registers.write_by_id(register_id::ah, std::uint8_t{42});
}

TEST_CASE("process::attach success", "[process]") {
  auto target = process::launch("targets/run_endlessly", false);
  auto proc = process::attach(target->pid());
  REQUIRE(get_process_status(target->pid()) == 't');
}

TEST_CASE("process::attach invalid PID", "[process]") {
  REQUIRE_THROWS_AS(process::attach(0), error);
}

TEST_CASE("process::resume success", "[process]") {
  {
    auto proc = process::launch("targets/run_endlessly");
    proc->resume();
    auto status = get_process_status(proc->pid());
    auto success = status == 'R' or status == 'S';
    REQUIRE(success);
  }
  {
    auto target = process::launch("targets/run_endlessly", false);
    auto proc = process::attach(target->pid());
    proc->resume();
    auto status = get_process_status(proc->pid());
    auto success = status == 'R' or status == 'S';
    REQUIRE(success);
  }
}

TEST_CASE("process::resume already terminated", "[process]") {
  auto proc = process::launch("targets/end_immediately");
  proc->resume();
  proc->wait_on_signal();
  REQUIRE_THROWS_AS(proc->resume(), error);
}
