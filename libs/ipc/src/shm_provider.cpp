#include "ipc/shm_provider.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdexcept>
#include <utility>

#include "ipc/shm_header.hpp"

namespace cere::ipc {

namespace {

void VerifyAttachSize(int shm_fd, std::size_t want, const std::string& name) {
  struct stat info{};
  if (fstat(shm_fd, &info) < 0) {
    throw std::runtime_error("fstat failed: " + name);
  }
  if (static_cast<std::size_t>(info.st_size) < want) {
    throw std::runtime_error("shm segment smaller than expected: " + name);
  }
}

void ApplyHeader(void* ptr, ShmProvider::Mode mode, const std::string& name) {
  auto* header = static_cast<ShmHeader*>(ptr);
  if (mode == ShmProvider::Mode::kCreate) {
    InitShmHeader(*header);
  } else if (!CheckShmHeader(*header)) {
    throw std::runtime_error("shm magic/version mismatch: " + name);
  }
}

}

ShmProvider::ShmProvider(std::string name, std::size_t size, Mode mode)
    : name_(std::move(name)), size_(size), mode_(mode) {
  const int flags = (mode == Mode::kCreate) ? (O_CREAT | O_RDWR) : O_RDWR;
  fd_ = shm_open(name_.c_str(), flags, kShmPerms);
  if (fd_ < 0) {
    throw std::runtime_error("shm_open failed: " + name_);
  }

  try {
    if (mode == Mode::kCreate) {
      if (ftruncate(fd_, static_cast<off_t>(size)) < 0) {
        throw std::runtime_error("ftruncate failed");
      }

      if (fchmod(fd_, kShmPerms) < 0) {
        throw std::runtime_error("fchmod failed");
      }
    }
    if (mode == Mode::kAttach) {
      VerifyAttachSize(fd_, size_, name_);
    }
    ptr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (ptr_ == MAP_FAILED) {
      ptr_ = nullptr;
      throw std::runtime_error("mmap failed");
    }
    ApplyHeader(ptr_, mode, name_);
  } catch (...) {
    if (ptr_ != nullptr) {
      munmap(ptr_, size_);
      ptr_ = nullptr;
    }
    close(fd_);
    fd_ = -1;
    if (mode == Mode::kCreate) {
      shm_unlink(name_.c_str());
    }
    throw;
  }
}

ShmProvider::~ShmProvider() {
  if (ptr_ != nullptr && ptr_ != MAP_FAILED) {
    munmap(ptr_, size_);
  }
  if (fd_ >= 0) {
    close(fd_);
  }
  if (mode_ == Mode::kCreate) {
    shm_unlink(name_.c_str());
  }
}

}
