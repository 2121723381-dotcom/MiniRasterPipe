# MiniRasterPipe

Software Rasterizer | 软件光栅化 | Phong 光照 | 双线性采样 | 凹凸贴图 | 位移贴图

完整软件光栅化渲染管线，纯 CPU 复现现代 GPU 渲染核心流程，覆盖 GAMES101 课程作业 1-4 的全部核心算法。

# 实现声明 Implementation Statement

## 一、基础框架来源说明
本项目基于 **GAMES101 现代计算机图形学入门** 课程作业的初始脚手架框架进行开发。该初始框架仅提供工程基础能力，不包含任何核心渲染算法的具体实现，具体提供内容为：
- 核心类的接口声明（`Triangle`、`rasterizer`、`Texture`、着色器载荷等头文件结构定义）
- Eigen、OpenCV 等第三方依赖库的编译配置与基础接入
- 第三方 OBJ 模型加载组件 `OBJ_Loader.h`
- 程序运行入口框架与作业输入输出规范
- 待填充的空函数入口（即课程作业的 TODO 占位逻辑）

## 二、核心算法独立实现说明
本项目中所有图形学核心算法、渲染管线逻辑与功能扩展均由本人独立编写完成，覆盖从几何变换到片元着色的完整流程。具体实现内容包括：

## 核心特性

- **完整渲染管线**：模型变换 → 视图变换 → 透视投影 → 视口变换 → 光栅化 → 片元着色全流程自研
- **三角形光栅化**：包围盒遍历、重心坐标插值、透视校正属性插值、Z-Buffer 深度测试
- **Phong 光照模型**：环境光 + 漫反射 + 高光反射，支持多光源与距离平方衰减
- **纹理映射**：OBJ 模型纹理加载，内置双线性采样平滑纹理，带边界越界保护
- **高级贴图**：
  - 凹凸贴图（Bump Mapping）：基于高度图 + TBN 切线空间扰动法线，模拟表面凹凸细节
  - 位移贴图（Displacement Mapping）：基于高度图修改顶点位置，真实改变几何轮廓
- **多着色器切换**：支持法线可视化、纯色 Phong、纹理着色、凹凸、位移 5 种渲染模式

## 编译与运行

### 环境依赖
- C++17 及以上
- OpenCV 4.x（图像读写与窗口显示）
- Eigen3（矩阵向量运算）

# 核心代码说明
## 一、几何变换矩阵（main.cpp）
1，视图矩阵
```cpp
Eigen::Matrix4f get_view_matrix(Eigen::Vector3f eye_pos) {
    Eigen::Matrix4f view = Eigen::Matrix4f::Identity();
    Eigen::Matrix4f translate;
    translate << 
        1, 0, 0, -eye_pos[0],
        0, 1, 0, -eye_pos[1], 
        0, 0, 1, -eye_pos[2],
        0, 0, 0, 1;
    view = translate * view; 
    return view;
}
```
2，模型矩阵
```cpp
Eigen::Matrix4f get_model_matrix(float angle) {
    Eigen::Matrix4f rotation;
    angle = angle * MY_PI / 180.f;
    rotation << 
        cos(angle), 0, sin(angle), 0,
        0, 1, 0, 0,
        -sin(angle), 0, cos(angle), 0,
        0, 0, 0, 1;
    Eigen::Matrix4f scale;
    scale << 
        2.5, 0, 0, 0,
        0, 2.5, 0, 0,
        0, 0, 2.5, 0,
        0, 0, 0, 1;
    Eigen::Matrix4f translate = Eigen::Matrix4f::Identity();
    return translate * rotation * scale;
}
```

3. 透视投影矩阵
```cpp
Eigen::Matrix4f get_projection_matrix(float eye_fov, float aspect_ratio, float zNear, float zFar) {
    float fov_rad = eye_fov * MY_PI / 180.0f;
    float top = tan(fov_rad / 2) * zNear;
    float bottom = -top;
    float right = top * aspect_ratio;
    float left = -right;

    Eigen::Matrix4f M_ortho = Eigen::Matrix4f::Identity();
    M_ortho(0, 3) = -(right + left) / 2; 
    M_ortho(1, 3) = -(top + bottom) / 2; 
    M_ortho(2, 3) = -(zNear + zFar) / 2; 
    M_ortho(0, 0) = 2.0f / (right - left); 
    M_ortho(1, 1) = 2.0f / (top - bottom); 
    M_ortho(2, 2) = 2.0f / (zNear - zFar); 

    Eigen::Matrix4f M_projection = Eigen::Matrix4f::Identity();
    M_projection(0, 0) = zNear;
    M_projection(1, 1) = zNear;
    M_projection(2, 2) = zNear + zFar;
    M_projection(2, 3) = -zNear * zFar;
    M_projection(3, 2) = -1;
    return M_projection * M_ortho;
}
```

## 二、三角形光栅化核心（rasterizer.cpp）
1. 三角形内点检测（叉积法）
```cpp
static bool insideTriangle(float x, float y, const Vector4f* _v){
    Vector3f v[3];
    for(int i=0;i<3;i++)
        v[i] = {_v[i].x(),_v[i].y(), 1.0};
    Vector3f f0,f1,f2;
    f0 = v[1].cross(v[0]);
    f1 = v[2].cross(v[1]);
    f2 = v[0].cross(v[2]);
    Vector3f p(x,y,1.);
    if((p.dot(f0)*f0.dot(v[2])>0) && (p.dot(f1)*f1.dot(v[0])>0) && (p.dot(f2)*f2.dot(v[1])>0))
        return true;
    return false;
}
```

2. 重心坐标计算
```cpp
static std::tuple<float, float, float> computeBarycentric2D(float x, float y, const Vector4f* v){
    float c1 = (x*(v[1].y() - v[2].y()) + (v[2].x() - v[1].x())*y + v[1].x()*v[2].y() - v[2].x()*v[1].y()) / (v[0].x()*(v[1].y() - v[2].y()) + (v[2].x() - v[1].x())*v[0].y() + v[1].x()*v[2].y() - v[2].x()*v[1].y());
    float c2 = (x*(v[2].y() - v[0].y()) + (v[0].x() - v[2].x())*y + v[2].x()*v[0].y() - v[0].x()*v[2].y()) / (v[1].x()*(v[2].y() - v[0].y()) + (v[0].x() - v[2].x())*v[1].y() + v[2].x()*v[0].y() - v[0].x()*v[2].y());
    float c3 = (x*(v[0].y() - v[1].y()) + (v[1].x() - v[0].x())*y + v[0].x()*v[1].y() - v[1].x()*v[0].y()) / (v[2].x()*(v[0].y() - v[1].y()) + (v[1].x() - v[0].x())*v[2].y() + v[0].x()*v[1].y() - v[1].x()*v[0].y());
    return {c1,c2,c3};
}
```

3. 光栅化主循环（包围盒 + 透视校正 + Z-Buffer）
```cpp
void rst::rasterizer::rasterize_triangle(const Triangle& t, const std::array<Eigen::Vector3f, 3>& view_pos)
{
	auto v = t.toVector4();
	float x_min = std::min({ v[0].x(), v[1].x(), v[2].x() });
	float y_min = std::min({ v[0].y(), v[1].y(), v[2].y() });
	float x_max = std::max({ v[0].x(), v[1].x(), v[2].x() });
	float y_max = std::max({ v[0].y(), v[1].y(), v[2].y() });

	for (int x = (int)x_min; x <= (int)x_max; x++)
	{
		for (int y = (int)y_min; y <= (int)y_max; y++)
		{
			float sx = x + 0.5f;
			float sy = y + 0.5f;
			if (insideTriangle(sx, sy, t.v))
			{
				auto[alpha, beta, gamma] = computeBarycentric2D(sx, sy, t.v);
				// 透视校正深度
				float Z = 1.0 / (alpha / v[0].w() + beta / v[1].w() + gamma / v[2].w());
				float zp = alpha * v[0].z() / v[0].w() + beta * v[1].z() / v[1].w() + gamma * v[2].z() / v[2].w();
				zp *= Z;
				// 透视校正属性插值权重
				float w0 = alpha * Z / v[0].w();
				float w1 = beta * Z / v[1].w();
				float w2 = gamma * Z / v[2].w();
				// 插值各项属性
				auto interpolated_shadingcoords = interpolate(w0, w1, w2, view_pos[0], view_pos[1], view_pos[2], 1);
				auto interpolated_color = interpolate(w0, w1, w2, t.color[0], t.color[1], t.color[2], 1);
				auto interpolated_normal = interpolate(w0, w1, w2, t.normal[0], t.normal[1], t.normal[2], 1).normalized();
				auto interpolated_texcoords = interpolate(w0, w1, w2, t.tex_coords[0], t.tex_coords[1], t.tex_coords[2], 1);

				int pixel_idx = get_index(x, y);
				// 深度测试
				if (zp < depth_buf[pixel_idx])
				{
					depth_buf[pixel_idx] = zp;
					fragment_shader_payload payload(
						interpolated_color,
						interpolated_normal,
						interpolated_texcoords,
						texture.has_value() ? &texture.value() : nullptr
					);
					payload.view_pos = interpolated_shadingcoords;
					Eigen::Vector3f final_color = fragment_shader(payload);
					Eigen::Vector2i point_pos(x, y);
					set_pixel(point_pos, final_color);
				}
			}
		}
	}
}
```

## 三、Phong 光照模型（main.cpp）
```cpp
Eigen::Vector3f phong_fragment_shader(const fragment_shader_payload& payload)
{
    Eigen::Vector3f ka = Eigen::Vector3f(0.005, 0.005, 0.005);
    Eigen::Vector3f kd = payload.color;
    Eigen::Vector3f ks = Eigen::Vector3f(0.7937, 0.7937, 0.7937);
    auto l1 = light{{20, 20, 20}, {500, 500, 500}};
    auto l2 = light{{-20, 20, 0}, {500, 500, 500}};
    std::vector<light> lights = {l1, l2};
    Eigen::Vector3f amb_light_intensity{10, 10, 10};
    Eigen::Vector3f eye_pos{0, 0, 10};
    float p = 150;
    Eigen::Vector3f point = payload.view_pos;
    Eigen::Vector3f normal = payload.normal;
    Eigen::Vector3f result_color = {0, 0, 0};

	for (auto& light : lights)
	{
		float distance_squared = (light.position - point).squaredNorm();
		Eigen::Vector3f light_dir = (light.position - point).normalized();
		Eigen::Vector3f view_dir = (eye_pos - point).normalized();
		Eigen::Vector3f half_dir = (light_dir + view_dir).normalized();
		float cos_theta = std::max(0.0f, normal.dot(light_dir));
		float s_cos_theta = std::max(0.0f, half_dir.dot(normal));
		auto diffuse = kd.cwiseProduct(light.intensity / distance_squared) * cos_theta;
		auto specular = ks.cwiseProduct(light.intensity / distance_squared) * std::pow(s_cos_theta, p);
		result_color += diffuse + specular;
	}
	auto ambient = ka.cwiseProduct(amb_light_intensity);
	result_color += ambient;
    return result_color * 255.f;
}
```

## 四、纹理双线性采样（Texture.hpp）
```cpp
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
    float ty = v_img - y0;w

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
```

## 五、凹凸贴图核心（main.cpp）
```cpp
Eigen::Vector3f bump_fragment_shader(const fragment_shader_payload& payload)
{
    Eigen::Vector3f normal = payload.normal;
    float kh = 0.2, kn = 0.1;
	float x = normal.x();
	float y = normal.y();
	float z = normal.z();

	// 构造切线空间 TBN 矩阵
	Eigen::Vector3f t(x*y / sqrt(x*x + z * z), sqrt(x*x + z * z), z*y / sqrt(x*x + z * z));
	t = t.normalized();
	Eigen::Vector3f b = normal.cross(t).normalized();
	Eigen::Matrix3f TBN;
	TBN << t.x(), b.x(), normal.x(),
		t.y(), b.y(), normal.y(),
		t.z(), b.z(), normal.z();

	float u = payload.tex_coords.x();
	float v = payload.tex_coords.y();
	int w = payload.texture->width;
	int h = payload.texture->height;

	// 双线性采样高度图
	auto get_height = [&](float u, float v) -> float {
		float u_img = u * w;
		float v_img = (1 - v) * h;
		int x0 = static_cast<int>(std::floor(u_img));
		int x1 = x0 + 1;
		int y0 = static_cast<int>(std::floor(v_img));
		int y1 = y0 + 1;
		x0 = clamp(x0, 0, w - 1);
		x1 = clamp(x1, 0, w - 1);
		y0 = clamp(y0, 0, h - 1);
		y1 = clamp(y1, 0, h - 1);
		auto get_color = [&](int x, int y) -> Eigen::Vector3f {
			float su = (float)x / w;
			float sv = 1.0f - (float)y / h;
			return payload.texture->getColor(su, sv);
		};
		float q00 = get_color(x0, y0)[0];
		float q10 = get_color(x1, y0)[0];
		float q01 = get_color(x0, y1)[0];
		float q11 = get_color(x1, y1)[0];
		float tx = u_img - x0;
		float ty = v_img - y0;
		float r1 = q00 * (1 - tx) + q10 * tx;
		float r2 = q01 * (1 - tx) + q11 * tx;
		return r1 * (1 - ty) + r2 * ty;
	};

	// 计算高度梯度，扰动法线
	float du = 1.0f / w;
	float dv = 1.0f / h;
	float dU = kh * kn * (get_height(u + du, v) - get_height(u, v));
	float dV = kh * kn * (get_height(u, v + dv) - get_height(u, v));
	Eigen::Vector3f ln (-dU, -dV, 1);
	Eigen::Vector3f new_normal = (TBN * ln).normalized();

    // 后续 Phong 光照使用 new_normal 计算（代码同 phong 着色器，此处省略重复部分）
}
```

## 六、位移贴图核心差异（main.cpp）
```cpp
// 沿法线方向偏移顶点位置，真实改变几何轮廓
point += kn * normal * get_height(u, v);
// 后续光照使用偏移后的 point 和扰动后的 new_normal 计算
```
