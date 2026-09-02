#ifndef __TREE_H__
#define __TREE_H__

#include <sstream>
#include <vector>
#include <iterator>
#include <map>
#include <string>
#include <utility>

#include "file_panel.h"
#include "spdlog/spdlog.h"

struct Tree {
Tree(const std::string &filename, const std::string &path, uint32_t modified)
    : name(filename)
    , full_path(path)
    , date_modified(modified)
    , has_metadata(false)
    , parent(this) {
  }

  ~Tree() {
    children.clear();
  }

  bool is_leaf() const {
    // this is just wrong, a folder without files is a still a
    // folder. lucky the files.list endpoint doesn't return empty
    // folders
    return children.empty();
  }
  
  Tree& find_or_create(const std::string &value, const std::string &path, uint32_t modified) {
    if (modified > date_modified) {
      date_modified = modified;
    }

    const auto &entry = children.find(value);
    if (entry != children.cend()) {
      return entry->second;
    }

    children.insert({value, Tree(value, path, modified)});
    Tree &child = children.find(value)->second;
    child.parent = this;
    return child;
  }

  void add_path(const std::vector<std::string>& paths, const std::string &path, uint32_t modified) {
    if (paths.empty()) {
      return;
    }

    Tree *cur_node = this;
    std::string cur_path;
    for (const auto &p : paths) {
      if (cur_path.length() > 0) {
	cur_path = fmt::format("{}/{}", cur_path, p);
      } else {
	cur_path = p;
      }

      cur_node = &(cur_node->find_or_create(p, cur_path, modified));
    }
  }

  Tree *find_path(const std::vector<std::string>& paths) {
    if (paths.empty()) {
      return this;
    }

    Tree *cur_node = this;
    for (const auto &p : paths) {
      const auto &entry = cur_node->children.find(p);
      if (entry != cur_node->children.cend()) {
	cur_node = &entry->second;
      } else {
	return this;
      }
    }
    return cur_node->is_leaf() ? this : cur_node;
  }
  

  Tree *get_child(const std::string child) {
    const auto &e = children.find(child);
    if (e != children.cend()) {
      return &e->second;
    }
    
    return NULL;
  }

  void set_name(const std::string &n) {
    name = n;
  }

  void traverse() const {
    if (!is_leaf()) {
      for (auto e = children.cbegin(); e != children.cend(); ++e) {
	e->second.traverse();
      }
    }
  }

  bool contains_metadata() {
    return has_metadata;
  }

  void set_metadata(json &j) {
    has_metadata = true;
    metadata = j;
  }

  void clear() {
    children.clear();
  }

  // Metadata survives the rebuild in PrintPanel::subscribe, which throws the
  // whole tree away and asks moonraker for the list again. Without this every
  // refresh re-requests server.files.metadata for whatever is on screen, and
  // moonraker announces a filelist change whenever it has to scan a file it has
  // no metadata for, so the refresh and the request feed each other. See
  // docs/audit.md C23.
  //
  // Keyed by full_path and qualified by date_modified, so a file replaced under
  // the same name misses and is scanned again.
  void collect_metadata(std::map<std::string, std::pair<uint32_t, json>> &out) const {
    if (has_metadata) {
      out.insert({full_path, {date_modified, metadata}});
    }

    for (const auto &c : children) {
      c.second.collect_metadata(out);
    }
  }

  void apply_metadata(const std::map<std::string, std::pair<uint32_t, json>> &in) {
    const auto &entry = in.find(full_path);
    if (entry != in.cend() && entry->second.first == date_modified) {
      has_metadata = true;
      metadata = entry->second.second;
    }

    for (auto &c : children) {
      c.second.apply_metadata(in);
    }
  }

  const char* get_thumbpath() {
    if (metadata.contains("result") && metadata["result"].contains("thumbnails")) {
      // XXX: fix my index
      return metadata["result"]["thumbnails"][1]["relative_path"].template get<std::string>().c_str();
    }
    return NULL;
  }
  
  std::string name;
  std::string full_path;
  uint32_t date_modified;
  json metadata;
  bool has_metadata;
  Tree *parent;
  std::map<std::string, Tree> children;
};

#endif // __TREE_H__
