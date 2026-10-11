#include <libsdb/breakpoint_site.hpp>
namespace {
auto get_next_id() {
  static sdb::breakpoint_site::id_type id = 0;
  return ++id;
}
}  // namespace

sdb::breakpoint_site::breakpoint_site(process& proc, virt_addr address)
    : id_{get_next_id()},
      process_{&proc},
      address_{address},
      is_enabled_{false},
      saved_data_{} {}