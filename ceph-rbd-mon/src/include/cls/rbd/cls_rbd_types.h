// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_CLS_RBD_TYPES_H
#define CEPH_CLS_RBD_TYPES_H

#include "types.h"
#include "utime.h"
#include "buffer.h"

namespace cls {
namespace rbd {

// 基本的RBD类型定义
struct MirrorMode {
  static const int MIRROR_MODE_DISABLED = 0;
  static const int MIRROR_MODE_IMAGE = 1;
  static const int MIRROR_MODE_POOL = 2;
};

struct MirrorPeer {
  std::string uuid;
  std::string cluster_name;
  std::string client_name;
  utime_t last_seen;
  
  MirrorPeer() : last_seen(0) {}
};

struct MirrorImage {
  std::string global_image_id;
  std::string peer_mirror_uuid;
  int state;
  
  MirrorImage() : state(0) {}
};

struct MirrorImageStatus {
  std::string global_image_id;
  int state;
  std::string description;
  utime_t last_update;
  
  MirrorImageStatus() : state(0), last_update(0) {}
};

struct SnapshotInfo {
  uint64_t id;
  std::string name;
  uint64_t image_size;
  utime_t timestamp;
  
  SnapshotInfo() : id(0), image_size(0), timestamp(0) {}
};

struct ImageInfo {
  uint64_t size;
  uint64_t obj_size;
  uint64_t num_objs;
  int order;
  std::string block_name_prefix;
  int64_t parent_pool;
  std::string parent_name;
  std::string parent_snapname;
  std::string parent_block_name_prefix;
  
  ImageInfo() : size(0), obj_size(0), num_objs(0), order(0), parent_pool(-1) {}
};

// 基本的编码/解码函数声明
void encode(const MirrorMode& mode, bufferlist& bl);
void decode(MirrorMode& mode, bufferlist::const_iterator& it);

void encode(const MirrorPeer& peer, bufferlist& bl);
void decode(MirrorPeer& peer, bufferlist::const_iterator& it);

void encode(const MirrorImage& image, bufferlist& bl);
void decode(MirrorImage& image, bufferlist::const_iterator& it);

void encode(const MirrorImageStatus& status, bufferlist& bl);
void decode(MirrorImageStatus& status, bufferlist::const_iterator& it);

void encode(const SnapshotInfo& info, bufferlist& bl);
void decode(SnapshotInfo& info, bufferlist::const_iterator& it);

void encode(const ImageInfo& info, bufferlist& bl);
void decode(ImageInfo& info, bufferlist::const_iterator& it);

} // namespace rbd
} // namespace cls

#endif // CEPH_CLS_RBD_TYPES_H
