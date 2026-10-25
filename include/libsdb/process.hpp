#ifndef SDB_PROCESS_HPP
#define SDB_PROCESS_HPP

#include <sys/types.h>

#include <cstdint>
#include <filesystem>
#include <libsdb/breakpoint_site.hpp>
#include <libsdb/registers.hpp>
#include <libsdb/stoppoint_collection.hpp>
#include <libsdb/types.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace sdb {
enum class process_state { stopped, running, exited, terminated };
struct stop_reason {
  stop_reason(int wait_status);

  process_state reason;
  std::uint8_t info;
};
class process {
 public:
  process() = delete;
  process(const process&) = delete;
  process& operator=(const process&) = delete;
  ~process();

  // Launch a new process and attach to it. If debug is true, the process will
  // be launched in a stopped state, otherwise it will be launched in a running
  // state.
  static std::unique_ptr<process> launch(
      std::filesystem::path path, bool debug = true,
      std::optional<int> stdout_replacement = std::nullopt);
  static std::unique_ptr<process> attach(pid_t pid);

  process_state state() const { return state_; }
  void resume();
  stop_reason wait_on_signal();
  pid_t pid() const { return pid_; }

  registers& get_registers() { return *registers_; }
  const registers& get_registers() const { return *registers_; }

  void write_user_area(std::size_t offset, std::uint64_t data);

  void write_fprs(const user_fpregs_struct& fprs);
  void write_gprs(const user_regs_struct& gprs);

  virt_addr get_pc() const {
    return virt_addr{
        get_registers().read_by_id_as<std::uint64_t>(register_id::rip)};
  }

  void set_pc(virt_addr address) {
    get_registers().write_by_id(register_id::rip, address.addr());
  }

  sdb::stop_reason step_instruction();

  breakpoint_site& create_breakpoint_site(virt_addr address);

  stoppoint_collection<breakpoint_site>& breakpoint_sites() {
    return breakpoint_sites_;
  }

  const stoppoint_collection<breakpoint_site>& breakpoint_sites() const {
    return breakpoint_sites_;
  }

  std::vector<std::byte> read_memory(virt_addr address,
                                     std::size_t amount) const;
  void write_memory(virt_addr address, span<const std::byte> data);

  template <class T>
  T read_memory_as(virt_addr address) const {
    auto data = read_memory(address, sizeof(T));
    return from_bytes<T>(data.data());
  }

 private:
  process(pid_t pid, bool terminate_on_end, bool is_attached)
      : pid_{pid},
        terminate_on_end_{terminate_on_end},
        is_attached_{is_attached},
        registers_{new registers(*this)} {}
  pid_t pid_ = 0;
  bool terminate_on_end_ = true;
  bool is_attached_ = true;
  process_state state_ = process_state::stopped;

  void read_all_registers();

  std::unique_ptr<registers> registers_;
  stoppoint_collection<breakpoint_site> breakpoint_sites_;
};
}  // namespace sdb
#endif