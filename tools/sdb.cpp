#include <editline/readline.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <libsdb/error.hpp>
#include <libsdb/process.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::unique_ptr<sdb::process> attach(int argc, const char** argv) {
  // Passing PID
  if (argc == 3 && argv[1] == std::string_view("-p")) {
    pid_t pid = std::atoi(argv[2]);
    return sdb::process::attach(pid);
  }
  // Passing program name
  else {
    const char* program_path = argv[1];
    return sdb::process::launch(program_path);
  }
}
std::vector<std::string> split(std::string_view str, char delimiter) {
  std::vector<std::string> out{};
  std::stringstream ss{std::string{str}};
  std::string item;
  while (std::getline(ss, item, delimiter)) {
    out.push_back(item);
  }
  return out;
}
bool is_prefix(std::string_view str, std::string_view of) {
  if (str.size() > of.size()) return false;
  return std::equal(str.begin(), str.end(), of.begin());
}
void resume(pid_t pid) {
  if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0) {
    std::cerr << "Couldn't continue\n";
    std::exit(EXIT_FAILURE);
  }
}
void wait_on_signal(pid_t pid) {
  int wait_status;
  int options = 0;
  if (waitpid(pid, &wait_status, options) < 0) {
    std::perror("waitpid failed");
    std::exit(EXIT_FAILURE);
  }
}
void print_stop_reason(const sdb::process& process, sdb::stop_reason reason) {
  std::cout << "Process " << process.pid() << ' ';

  switch (reason.reason) {
    case sdb::process_state::exited:
      std::cout << "exited with status " << static_cast<int>(reason.info);
      break;
    case sdb::process_state::terminated:
      std::cout << "terminated with signal " << sigabbrev_np(reason.info);
      break;
    case sdb::process_state::stopped:
      std::cout << "stopped with signal " << sigabbrev_np(reason.info);
      break;
  }

  std::cout << std::endl;
}

void print_help(const std::vector<std::string>& args) {
  if (args.size() == 1) {
    std::cerr << R"(Available commands:
    continue    - Resume the process
    register    - Commands for operating on registers
    help        - Show this help message
)";
  } else if (is_prefix(args[1], "register")) {
    std::cerr << R"(Available commands:
    read
    read <register>
    read all
    write <register> <value>
)";
  } else {
    std::cout << "No help available on that\n";
  }
}

void handle_register_read(sdb::process& process,
                          const std::vector<std::string>& args) {
  auto format = [](auto t) {
    if constexpr (std::is_floating_point_v<decltype(t)>) {
      return fmt::format("{}", t);
    } else if constexpr (std::is_integral_v<decltype(t)>) {
      return fmt::format("{:#0{}x}", t, sizeof(t) * 2 + 2);
    } else {
      return fmt::format("[{:#04x}]", fmt::join(t, ","));
    }
  };
  if (args.size() == 2 or (args.size() == 3 && args[2] == "all")) {
    for (auto& info : sdb::g_register_infos) {
      auto should_print =
          (args.size() == 3 or info.type == sdb::register_type::gpr) and
          info.name != "orig_rax";
      if (!should_print) continue;
      auto value = process.get_registers().read(info);
      fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
    }
  } else if (args.size() == 3) {
    try {
      auto info = sdb::register_info_by_name(args[2]);
      auto value = process.get_registers().read(info);
      fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
    } catch (const sdb::error& err) {
      std::cerr << "No such register\n";
      return;
    }
  } else {
    print_help({"help", "register"});
  }
}

void handle_register_command(sdb::process& process,
                             const std::vector<std::string>& args) {
  if (args.size() < 2) {
    print_help({"help", "register"});
    return;
  }
  if (is_prefix(args[1], "read")) {
    handle_register_read(process, args);
  } else if (is_prefix(args[1], "write")) {
    handle_register_write(process, args);
  } else {
    print_help({"help", "register"});
  }
}

void handle_command(std::unique_ptr<sdb::process>& process,
                    std::string_view line) {
  auto args = split(line, ' ');
  auto command = args[0];
  if (is_prefix(command, "continue")) {
    process->resume();
    auto reason = process->wait_on_signal();
    print_stop_reason(*process, reason);
  } else if (is_prefix(command, "help")) {
    print_help(args);
  } else if (is_prefix(command, "register")) {
    handle_register_command(*process, args);
  } else {
    std::cerr << "Unknown command\n";
  }
}
void main_loop(std::unique_ptr<sdb::process>& process) {
  char* line = nullptr;
  while ((line = readline("sdb> ")) != nullptr) {
    std::string line_str;

    if (line == std::string_view("")) {
      free(line);
      if (history_length > 0) {
        line_str = history_list()[history_length - 1]->line;
      }
    } else {
      line_str = line;
      add_history(line);
      free(line);
    }
    if (!line_str.empty()) {
      try {
        handle_command(process, line_str);
      } catch (const sdb::error& err) {
        std::cout << err.what() << '\n';
      }
    }
  }
}
}  // namespace

int main(int argc, const char** argv) {
  if (argc == 1) {
    std::cerr << "No arguments given\n";
    return EXIT_FAILURE;
  }

  try {
    auto process = attach(argc, argv);
    main_loop(process);
  } catch (const sdb::error& err) {
    std::cout << err.what() << '\n';
  }
}
