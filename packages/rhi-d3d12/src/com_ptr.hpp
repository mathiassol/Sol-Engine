#pragma once

namespace engine::rhi::d3d12 {

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* ptr) : ptr_(ptr) {}
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    void reset(T* ptr = nullptr) {
        if (ptr_) {
            ptr_->Release();
        }
        ptr_ = ptr;
    }

    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    T* detach() {
        T* ptr = ptr_;
        ptr_ = nullptr;
        return ptr;
    }

    T** put() {
        reset();
        return &ptr_;
    }

private:
    T* ptr_ = nullptr;
};

} // namespace engine::rhi::d3d12
