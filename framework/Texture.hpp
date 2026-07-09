////
//// Created by LEI XU on 4/27/19.
////
//
//#ifndef RASTERIZER_TEXTURE_H
//#define RASTERIZER_TEXTURE_H
//#include "global.hpp"
//#include <Eigen/Eigen>
//#include <opencv2/opencv.hpp>
//class Texture{
//private:
//    cv::Mat image_data;
//
//public:
//    Texture(const std::string& name)
//    {
//        image_data = cv::imread(name);
//        cv::cvtColor(image_data, image_data, cv::COLOR_BGR2RGB);
//        width = image_data.cols;
//        height = image_data.rows;
//    }
//
//    int width, height;
//
//    Eigen::Vector3f getColor(float u, float v)
//    {
//        auto u_img = u * width;
//        auto v_img = (1 - v) * height;
//        auto color = image_data.at<cv::Vec3b>(v_img, u_img);
//        return Eigen::Vector3f(color[0], color[1], color[2]);
//    }
//
//};
//#endif //RASTERIZER_TEXTURE_H
//
// Created by LEI XU on 4/27/19.
//
#ifndef RASTERIZER_TEXTURE_H
#define RASTERIZER_TEXTURE_H
#include "global.hpp"
#include <Eigen/Eigen>
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <iostream>

class Texture {
private:
	cv::Mat image_data;

	// 安全clamp函数，防止坐标越界
	template<typename T>
	T clamp(T value, T min_val, T max_val) const {
		return std::max(min_val, std::min(value, max_val));
	}

public:
	Texture(const std::string& name)
	{
		image_data = cv::imread(name);
		// 【关键修复1】检查纹理是否加载成功，路径错误直接提示
		if (image_data.empty()) {
			std::cerr << "【致命错误】纹理图片加载失败！路径：" << name << std::endl;
			std::cerr << "请检查程序运行的工作目录下是否存在该文件！" << std::endl;
			exit(-1);
		}
		cv::cvtColor(image_data, image_data, cv::COLOR_BGR2RGB);
		width = image_data.cols;
		height = image_data.rows;
		std::cout << "【纹理加载成功】宽=" << width << ", 高=" << height << std::endl;
	}

	int width, height;

	// 【核心修复】带边界保护的纹理采样，彻底解决越界崩溃
	Eigen::Vector3f getColor(float u, float v)
	{
		// 1. 强制把uv锁死在[0,1]，防止插值导致的越界
		u = clamp(u, 0.0f, 1.0f);
		v = clamp(v, 0.0f, 1.0f);

		// 2. 坐标计算：用width-1防止u=1.0时刚好越界（Mat索引是0~width-1）
		float u_img = u * (width - 1);
		float v_img = (1.0f - v) * (height - 1);

		// 3. 浮点转int+二次边界保护，解决浮点精度问题
		int x = static_cast<int>(std::round(u_img));
		int y = static_cast<int>(std::round(v_img));
		x = clamp(x, 0, width - 1);
		y = clamp(y, 0, height - 1);

		// 4. 安全访问像素（注意OpenCV是 行(y)在前，列(x)在后，不要写反！）
		auto color = image_data.at<cv::Vec3b>(y, x);
		return Eigen::Vector3f(color[0], color[1], color[2]);
	}

	// 【可选补充】给凹凸/位移贴图用的安全双线性采样，避免重复写代码
	Eigen::Vector3f getColorBilinear(float u, float v)
	{
		u = clamp(u, 0.0f, 1.0f);
		v = clamp(v, 0.0f, 1.0f);

		float u_img = u * (width - 1);
		float v_img = (1.0f - v) * (height - 1);

		int x0 = static_cast<int>(std::floor(u_img));
		int x1 = std::min(x0 + 1, width - 1);
		int y0 = static_cast<int>(std::floor(v_img));
		int y1 = std::min(y0 + 1, height - 1);

		float tx = u_img - x0;
		float ty = v_img - y0;

		auto q00 = image_data.at<cv::Vec3b>(y0, x0);
		auto q10 = image_data.at<cv::Vec3b>(y0, x1);
		auto q01 = image_data.at<cv::Vec3b>(y1, x0);
		auto q11 = image_data.at<cv::Vec3b>(y1, x1);

		Eigen::Vector3f c00(q00[0], q00[1], q00[2]);
		Eigen::Vector3f c10(q10[0], q10[1], q10[2]);
		Eigen::Vector3f c01(q01[0], q01[1], q01[2]);
		Eigen::Vector3f c11(q11[0], q11[1], q11[2]);

		auto r1 = c00 * (1 - tx) + c10 * tx;
		auto r2 = c01 * (1 - tx) + c11 * tx;
		return r1 * (1 - ty) + r2 * ty;
	}
};
#endif //RASTERIZER_TEXTURE_H