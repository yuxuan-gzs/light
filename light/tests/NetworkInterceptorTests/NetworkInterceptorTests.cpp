#include "../../NetworkInterceptor.h"
#include <thread>
#include <chrono>
#include <iostream>

int main() {
	auto interceptor = NetworkInterceptor::GetInstance();
	if (!interceptor->Start()) {
		std::cout << "Start failed" << std::endl;
		return 1;
	}

	// 添加一个临时阻止规则用于测试
	interceptor->BlockDomain("example.com", 0);

	// 等待一段时间让拦截线程运行并捕获流量
	std::this_thread::sleep_for(std::chrono::seconds(10));

	auto stats = interceptor->GetStats();
	std::cout << "Blocked packets: " << stats.blockedPackets << std::endl;

	interceptor->Stop();
	return 0;
}
