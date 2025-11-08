#pragma once


/**
 * Mixin design mode, concept class
 * Provide clear semantic & Promotes code reuse
 * 
 * Delete copy constructor and copy assigner
 */
class Noncopyable{
public:
    Noncopyable(const Noncopyable&) = delete;
    Noncopyable& operator=(const Noncopyable&) = delete;

/* disable instantiation without the cost of Abstract class (virtual func table) */
/* dont need polymorphism behavior */
protected:
    Noncopyable() = default;
    ~Noncopyable() = default;

};