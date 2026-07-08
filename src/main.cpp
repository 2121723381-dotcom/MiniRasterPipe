#include <iostream>
#include <opencv2/opencv.hpp>
#include "global.hpp"
#include "rasterizer.hpp"
#include "Triangle.hpp"
#include "Shader.hpp"
#include "Texture.hpp"
#include "OBJ_Loader.h"

// �ֶ�ʵ�� clamp ���������� C++11/14���� std::clamp ������ȫһ�£�
template<typename T>
T clamp(T value, T min_val, T max_val) {
	return std::max(min_val, std::min(value, max_val));
}

// --- 1. ��ͼ���� (View Matrix) ---
// ���ã������硰�ᡱ���������ǰ
Eigen::Matrix4f get_view_matrix(Eigen::Vector3f eye_pos) {
    // ��ʼ��һ����λ����ɶҲ���ɵľ���
    Eigen::Matrix4f view = Eigen::Matrix4f::Identity();
    
    // ����ƽ�ƾ���
    // �����߼���������� (x, y, z)��Ϊ�����������ԭ�㣬����͵��� (-x, -y, -z) �ƶ�
    Eigen::Matrix4f translate;
    translate << 
        1, 0, 0, -eye_pos[0],
        0, 1, 0, -eye_pos[1], 
        0, 0, 1, -eye_pos[2],
        0, 0, 0, 1;
    
    // ���أ�������ӽ��µ�����
    view = translate * view; 
    return view;
}

// --- 2. ģ�;��� (Model Matrix) ---
// ���ã�������������������ôת����ô����
Eigen::Matrix4f get_model_matrix(float angle) {
    // 1. ��ת������ Y ��ת (��ģ���Լ�תȦȦ)
    Eigen::Matrix4f rotation;
    angle = angle * MY_PI / 180.f; // �Ƕ�ת����
    rotation << 
        cos(angle), 0, sin(angle), 0,
        0, 1, 0, 0,
        -sin(angle), 0, cos(angle), 0,
        0, 0, 0, 1;

    // 2. ���ž��󣺰�ģ�ͷŴ� 2.5 ��
    Eigen::Matrix4f scale;
    scale << 
        2.5, 0, 0, 0,
        0, 2.5, 0, 0,
        0, 0, 2.5, 0,
        0, 0, 0, 1;

    // 3. ƽ�ƾ�������û�� (��λ����)
    Eigen::Matrix4f translate = Eigen::Matrix4f::Identity();

    // ���أ����� * ��ת * ƽ�� (ע��˳�������ı任���ұ�)
    return translate * rotation * scale;
}

// --- 3. ͶӰ���� (Projection Matrix) ---
// ���ã���������ϡ�����ԶС����͸��Ч��
Eigen::Matrix4f get_projection_matrix(float eye_fov, float aspect_ratio, float zNear, float zFar) {
    // 1. ����͸��ͶӰ�Ĳ��� (����̨���������)
    float fov_rad = eye_fov * MY_PI / 180.0f;
    float top = tan(fov_rad / 2) * zNear; // ��ƽ����ϱ߽�
    float bottom = -top;                 // �±߽�
    float right = top * aspect_ratio;    // �ұ߽� (������Ļ���߱�)
    float left = -right;                 // ��߽�

    // 2. ��������ͶӰ���� (Orthographic Projection)
    // Ŀ�ģ��Ѹղ���������Ǹ���׶�壬Ӳ��������ѹ������׼������ (-1 �� 1)
    Eigen::Matrix4f M_ortho = Eigen::Matrix4f::Identity();
    
    // ����ƽ�ƣ�����׶�������Ƶ�ԭ��
    M_ortho(0, 3) = -(right + left) / 2; 
    M_ortho(1, 3) = -(top + bottom) / 2; 
    M_ortho(2, 3) = -(zNear + zFar) / 2; 

    // �������ţ�����׶���С���ŵ� 2x2x2
    M_ortho(0, 0) = 2.0f / (right - left); 
    M_ortho(1, 1) = 2.0f / (top - bottom); 
    M_ortho(2, 2) = 2.0f / (zNear - zFar); 

    // 3. ����͸��ͶӰ���� (���ģ��޸� W ������ʵ�ֽ���ԶС)
	//���Զ��㾭��͸��ͶӰ����� w�ᴢ���Ӧ�����ֵ
    Eigen::Matrix4f M_projection = Eigen::Matrix4f::Identity();
    M_projection(0, 0) = zNear;           // X ����͸��
    M_projection(1, 1) = zNear;           // Y ����͸��
    M_projection(2, 2) = zNear + zFar;    // Z ������
    M_projection(2, 3) = -zNear * zFar;   // Z ����ƫ��
    M_projection(3, 2) = -1;             // �ؼ����� Z ֵ�浽 W �����͸�ӳ���

    // ���أ�͸�Ӿ��� * ��������
    return M_projection * M_ortho;
}

//vertex_shader��������ɫ�� ����paylod�Ƕ���
Eigen::Vector3f vertex_shader(const vertex_shader_payload& payload)
{
    return payload.position;
}

//normal_fragment_shader��ƬԪ����������ɫ�� ����Ĳ����Ǿ�����ֵ�� λ�ã����ߣ����������ݰ�
//���߱���Ӱ���������� Ӧ������Щ�ط�����
Eigen::Vector3f normal_fragment_shader(const fragment_shader_payload& payload)
{
	//(payload.normal.head<3>().normalized()��ȡ�����ݰ�payload��������ǰ��������
	//�����������ķ�Χ��[-1,1]��Ϊ��Ҫת������ɫ��Ϣ��Ҫ���Ʒ�Χ��[0,1]������+1��/2
    Eigen::Vector3f return_color = (payload.normal.head<3>().normalized() + Eigen::Vector3f(1.0f, 1.0f, 1.0f)) / 2.f;
    Eigen::Vector3f result;
	//��Ϊ�涨Xָ���ͨ����Yָ����ͨ����Zָ����ͨ�����ѷ������������ɫ��Ϣ��3D->2D,��Ч���淨��
    result << return_color.x() * 255, return_color.y() * 255, return_color.z() * 255;
    return result;
}

//���㷴����ߵĵ�λ���� vec�����䷽��ĵ�λ������axis������������ĵ�λ����
//�����ܵ�vecָ���Ǵӱ���ָ���Դ�ĵ�λ����=====�����Ƶ�����ͼ
static Eigen::Vector3f reflect(const Eigen::Vector3f& vec, const Eigen::Vector3f& axis)
{
    auto costheta = vec.dot(axis); //���������ʹ��dot(axis)�����������������cos
    return (2 * costheta * axis - vec).normalized();
}

//��Դ�ṹ�壻����λ���Լ���ǿ��
struct light
{
    Eigen::Vector3f position;
    Eigen::Vector3f intensity;
};

Eigen::Vector3f texture_fragment_shader(const fragment_shader_payload& payload)
{
    Eigen::Vector3f return_color = {0, 0, 0};
    if (payload.texture)//�������ͼ����
    {
        // TODO: Get the texture value at the texture coordinates of the current fragment
		float u = payload.tex_coords(0);
		float v = payload.tex_coords(1);

		return_color = payload.texture->getColor(u, v); //������ָ������
    }
    Eigen::Vector3f texture_color;
    texture_color << return_color.x(), return_color.y(), return_color.z();

    Eigen::Vector3f ka = Eigen::Vector3f(0.005, 0.005, 0.005);
    Eigen::Vector3f kd = texture_color / 255.f;
    Eigen::Vector3f ks = Eigen::Vector3f(0.7937, 0.7937, 0.7937);

    auto l1 = light{{20, 20, 20}, {500, 500, 500}};
    auto l2 = light{{-20, 20, 0}, {500, 500, 500}};

    std::vector<light> lights = {l1, l2};
    Eigen::Vector3f amb_light_intensity{10, 10, 10};
    Eigen::Vector3f eye_pos{0, 0, 10};

    float p = 150;

    Eigen::Vector3f color = texture_color;
    Eigen::Vector3f point = payload.view_pos;
    Eigen::Vector3f normal = payload.normal;

    Eigen::Vector3f result_color = {0, 0, 0};

	for (auto& light : lights)
	{
		// TODO: For each light source in the code, calculate what the *ambient*, *diffuse*, and *specular* 
		// components are. Then, accumulate that result on the *result_color* object.
		float distance_squared = (light.position - point).squaredNorm();

		Eigen::Vector3f light_dir = (light.position - point).normalized();
		Eigen::Vector3f view_dir = (eye_pos - point).normalized();//��������
		Eigen::Vector3f half_dir = (light_dir + view_dir).normalized();//������� ����Ҫ/2��Ϊ����Ҫ�����

		//std::max Ҫ�������������ͱ�����ȫ��ͬ ����0�����f
		float cos_theta = std::max(0.0f, normal.dot(light_dir));
		//����߹���ĵ�ˣ�half_dir��view_dir��
		float s_cos_theta = std::max(0.0f, half_dir.dot(normal));
		auto diffuse = kd.cwiseProduct(light.intensity / distance_squared) * cos_theta;

		auto specular = ks.cwiseProduct(light.intensity / distance_squared) * std::pow(s_cos_theta, p);
		result_color += diffuse + specular;
	}
	auto ambient = ka.cwiseProduct(amb_light_intensity);
	result_color += ambient;
    return result_color * 255.f;
}

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

    Eigen::Vector3f color = payload.color;
    Eigen::Vector3f point = payload.view_pos;
    Eigen::Vector3f normal = payload.normal;

    Eigen::Vector3f result_color = {0, 0, 0};
	for (auto& light : lights)
	{
		// TODO: For each light source in the code, calculate what the *ambient*, *diffuse*, and *specular* 
		// components are. Then, accumulate that result on the *result_color* object.
		float distance_squared = (light.position - point).squaredNorm();

		Eigen::Vector3f light_dir = (light.position - point).normalized();
		Eigen::Vector3f view_dir = (eye_pos - point).normalized();//��������
		Eigen::Vector3f half_dir = (light_dir + view_dir).normalized();//������� ����Ҫ/2��Ϊ����Ҫ�����

		//std::max Ҫ�������������ͱ�����ȫ��ͬ ����0�����f
		float cos_theta = std::max(0.0f, normal.dot(light_dir));
		//����߹���ĵ�ˣ�half_dir��view_dir��
		float s_cos_theta = std::max(0.0f, half_dir.dot(normal));
		auto diffuse = kd.cwiseProduct(light.intensity / distance_squared) * cos_theta;

		auto specular = ks.cwiseProduct(light.intensity / distance_squared) * std::pow(s_cos_theta, p);
		result_color += diffuse + specular;
	}
	auto ambient = ka.cwiseProduct(amb_light_intensity);
	result_color += ambient;
    return result_color * 255.f;
}


Eigen::Vector3f displacement_fragment_shader(const fragment_shader_payload& payload)
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

    Eigen::Vector3f color = payload.color; 
    Eigen::Vector3f point = payload.view_pos;
    Eigen::Vector3f normal = payload.normal;

    float kh = 0.2, kn = 0.1;
    
    // TODO: Implement displacement mapping here
    // Let n = normal = (x, y, z)
    // Vector t = (x*y/sqrt(x*x+z*z),sqrt(x*x+z*z),z*y/sqrt(x*x+z*z))
    // Vector b = n cross product t
    // Matrix TBN = [t b n]
    // dU = kh * kn * (h(u+1/w,v)-h(u,v))
    // dV = kh * kn * (h(u,v+1/h)-h(u,v))
    // Vector ln = (-dU, -dV, 1)
    // Position p = p + kn * n * h(u,v)
    // Normal n = normalize(TBN * ln)

	// TODO: Implement bump mapping here
	float x = normal.x();
	float y = normal.y();
	float z = normal.z();
	//�����������߶�ͼ�� u������뷨��n��ֱ������
	Eigen::Vector3f t(x*y / sqrt(x*x + z * z), sqrt(x*x + z * z), z*y / sqrt(x*x + z * z));
	t = t.normalized();
	//�����������߶�ͼ�� v������뷨��n��ֱ������
	Eigen::Vector3f b = normal.cross(t).normalized();
	Eigen::Matrix3f TBN;
	TBN << t.x(), b.x(), normal.x(),
		t.y(), b.y(), normal.y(),
		t.z(), b.z(), normal.z();

	float u = payload.tex_coords.x();   // ����U����
	float v = payload.tex_coords.y();   // ����V����
	int w = payload.texture->width;     // ��������(����)
	int h = payload.texture->height;    // �����߶�(����)

	//��������ͼ�ĻҶ�ֵ��Ϣת���ɸ߶���Ϣ
	// ===================== �������Texture.hppʵ�ֵ�˫���Բ��� =====================
	auto get_height = [&](float u, float v) -> float {
		float u_img = u * w;
		float v_img = (1 - v) * h;

		//floor�ذ庯������u_img����ȡ������static_cast<int>ת������
		int x0 = static_cast<int>(std::floor(u_img));
		int x1 = x0 + 1;
		int y0 = static_cast<int>(std::floor(v_img));
		int y1 = y0 + 1;

		x0 = clamp(x0, 0, w - 1);
		x1 = clamp(x1, 0, w - 1);
		y0 = clamp(y0, 0, h - 1);
		y1 = clamp(y1, 0, h - 1);

		//��[&] ��ζ�ź����ڲ�������������ʹ���ⲿ����
		auto get_color = [&](int x, int y) -> Eigen::Vector3f {
			float su = (float)x / w;
			float sv = 1.0f - (float)y / h;
			return payload.texture->getColor(su, sv);
		};
		//��ȡ�Ҷȣ��߶���Ϣ��
		float q00 = get_color(x0, y0)[0];
		float q10 = get_color(x1, y0)[0];
		float q01 = get_color(x0, y1)[0];
		float q11 = get_color(x1, y1)[0];

		//���ݲ����㸽��4������q��������Բ�ֵ���� ����������ƽ���߶�
		float tx = u_img - x0;
		float ty = v_img - y0;
		float r1 = q00 * (1 - tx) + q10 * tx;
		float r2 = q01 * (1 - tx) + q11 * tx;
		return r1 * (1 - ty) + r2 * ty;
	};

	//���������߶�ͼU�����»Ҷ�ֵ���߶�ֵ�ı仯����Χ[0,1]
	float dU = kh * kn * (get_height(u + 1 / w, v) - get_height(u, v));
	//���������߶�ͼV�����»Ҷ�ֵ���߶�ֵ�ı仯����Χ[0,1]
	float dV = kh * kn * (get_height(u, v + 1 / h) - get_height(u, v));

	//�����Ŷ����� Ҳ���Ǳ仯��ķ���
	Eigen::Vector3f ln(-dU, -dV, 1);
	Eigen::Vector3f new_normal;

	//TBN�����߿ռ�ת����ȫ������ľ���  ����õ���ȫ������ķ���
	new_normal = (TBN * ln).normalized();
	point += kn * normal*get_height(u, v);

    Eigen::Vector3f result_color = {0, 0, 0};

    for (auto& light : lights)
    {
		// TODO: For each light source in the code, calculate what the *ambient*, *diffuse*, and *specular* 
		// components are. Then, accumulate that result on the *result_color* object.
		float distance_squared = (light.position - point).squaredNorm();

		Eigen::Vector3f light_dir = (light.position - point).normalized();
		Eigen::Vector3f view_dir = (eye_pos - point).normalized();//��������
		Eigen::Vector3f half_dir = (light_dir + view_dir).normalized();//������� ����Ҫ/2��Ϊ����Ҫ�����

		//std::max Ҫ�������������ͱ�����ȫ��ͬ ����0�����f
		float cos_theta = std::max(0.0f, new_normal.dot(light_dir)); 
		//����߹���ĵ�ˣ�half_dir��view_dir��
		float s_cos_theta = std::max(0.0f, half_dir.dot(new_normal));
		auto diffuse = kd.cwiseProduct(light.intensity / distance_squared) * cos_theta;

		auto specular = ks.cwiseProduct(light.intensity / distance_squared) * std::pow(s_cos_theta, p);
		result_color += diffuse + specular;
    }
	auto ambient = ka.cwiseProduct(amb_light_intensity);
	result_color += ambient;
    return result_color * 255.f;
}

//��͹��ͼʵ��
//�����������Ǳ�������߶�ͼ Ȼ�� ͨ������ķ���ȷ�����߿ռ��λ�� 
//Ȼ�������λ��ʹ��˫���Բ�����ȡ��ĸ߶���Ϣ
//Ȼ���u v+1���Du Dv�����������õ��и߶Ȳ��ķ��� ����й��ռ���
Eigen::Vector3f bump_fragment_shader(const fragment_shader_payload& payload)
{
    
    Eigen::Vector3f ka = Eigen::Vector3f(0.005, 0.005, 0.005);
    Eigen::Vector3f kd = payload.color;
    Eigen::Vector3f ks = Eigen::Vector3f(0.7937, 0.7937, 0.7937);

    auto l1 = light{{20, 20, 20}, {500, 500, 500}};
    auto l2 = light{{-20, 20, 0}, {500, 500, 500}};

	// <light>���������ֻ��װ light ���͵Ķ���
    std::vector<light> lights = {l1, l2};
    Eigen::Vector3f amb_light_intensity{10, 10, 10};
    Eigen::Vector3f eye_pos{0, 0, 10};

    float p = 150;

    Eigen::Vector3f color = payload.color; 
    Eigen::Vector3f point = payload.view_pos;
    Eigen::Vector3f normal = payload.normal;


    float kh = 0.2, kn = 0.1; //�߶�����ϵ�� ��������ϵ��

    // TODO: Implement bump mapping here
	float x = normal.x();
	float y = normal.y();
	float z = normal.z();
	//�����������߶�ͼ�� u������뷨��n��ֱ������
	Eigen::Vector3f t (x*y / sqrt(x*x + z * z), sqrt(x*x + z * z), z*y / sqrt(x*x + z * z));
	t = t.normalized();
	//�����������߶�ͼ�� v������뷨��n��ֱ������
	Eigen::Vector3f b = normal.cross(t).normalized();
	Eigen::Matrix3f TBN;
	TBN << t.x(), b.x(), normal.x(),
		t.y(), b.y(), normal.y(),
		t.z(), b.z(), normal.z();

	float u = payload.tex_coords.x();   // ����U����
	float v = payload.tex_coords.y();   // ����V����
	int w = payload.texture->width;     // ��������(����)
	int h = payload.texture->height;    // �����߶�(����)

	//��������ͼ�ĻҶ�ֵ��Ϣת���ɸ߶���Ϣ
	// ===================== �������Texture.hppʵ�ֵ�˫���Բ��� =====================
	auto get_height = [&](float u, float v) -> float {
		float u_img = u * w;
		float v_img = (1 - v) * h;

		//floor�ذ庯������u_img����ȡ������static_cast<int>ת������
		int x0 = static_cast<int>(std::floor(u_img));
		int x1 = x0 + 1;
		int y0 = static_cast<int>(std::floor(v_img));
		int y1 = y0 + 1;

		x0 = clamp(x0, 0, w - 1);
		x1 = clamp(x1, 0, w - 1);
		y0 = clamp(y0, 0, h - 1);
		y1 = clamp(y1, 0, h - 1);

		//��[&] ��ζ�ź����ڲ�������������ʹ���ⲿ����
		auto get_color = [&](int x, int y) -> Eigen::Vector3f {
			float su = (float)x / w;
			float sv = 1.0f - (float)y / h;
			return payload.texture->getColor(su, sv);
		};
		//��ȡ�Ҷȣ��߶���Ϣ��
		float q00 = get_color(x0, y0)[0];
		float q10 = get_color(x1, y0)[0];
		float q01 = get_color(x0, y1)[0];
		float q11 = get_color(x1, y1)[0];

		//���ݲ����㸽��4������q��������Բ�ֵ���� ����������ƽ���߶�
		float tx = u_img - x0;
		float ty = v_img - y0;
		float r1 = q00 * (1 - tx) + q10 * tx;
		float r2 = q01 * (1 - tx) + q11 * tx;
		return r1 * (1 - ty) + r2 * ty;
	};

	//ֱ�Ӷ�u+1�������� Ҫ�������ڸ߶�ͼ����ı仯1 ���Լӵ���1/w dv
	//dvͬ��
	float du = 1.0f / w;
	float dv = 1.0f / h;
	float dU = kh * kn * (get_height(u + du, v) - get_height(u, v));
	float dV = kh * kn * (get_height(u, v + dv) - get_height(u, v));

	//�����Ŷ����� Ҳ���Ǳ仯��ķ���
	Eigen::Vector3f ln (-dU, -dV, 1);
	Eigen::Vector3f new_normal;

	//TBN�����߿ռ�ת����ȫ������ľ���  ����õ���ȫ������ķ���
	new_normal = (TBN * ln).normalized();
	
    Eigen::Vector3f result_color = {0, 0, 0};
	for (auto& light : lights)
	{
		// TODO: For each light source in the code, calculate what the *ambient*, *diffuse*, and *specular* 
		// components are. Then, accumulate that result on the *result_color* object.
		float distance_squared = (light.position - point).squaredNorm();

		Eigen::Vector3f light_dir = (light.position - point).normalized();
		Eigen::Vector3f view_dir = (eye_pos - point).normalized();//��������
		Eigen::Vector3f half_dir = (light_dir + view_dir).normalized();//������� ����Ҫ/2��Ϊ����Ҫ�����

		//std::max Ҫ�������������ͱ�����ȫ��ͬ ����0�����f
		float cos_theta = std::max(0.0f, new_normal.dot(light_dir));
		//����߹���ĵ�ˣ�half_dir��view_dir��
		float s_cos_theta = std::max(0.0f, new_normal.dot(half_dir));
		auto diffuse = kd.cwiseProduct(light.intensity / distance_squared) * cos_theta;

		auto specular = ks.cwiseProduct(light.intensity / distance_squared) * std::pow(s_cos_theta, p);
		result_color += diffuse + specular;
	}
	auto ambient = ka.cwiseProduct(amb_light_intensity);
	result_color += ambient;
	return result_color * 255.f;
}

int main(int argc, const char** argv)
{
    std::vector<Triangle*> TriangleList;

    float angle = 140.0;
    bool command_line = false;

    std::string filename = "output.png";
    objl::Loader Loader;
    std::string obj_path = "models/spot/";

    // Load .obj File
    bool loadout = Loader.LoadFile("models/spot/spot_triangulated_good.obj");
    for(auto mesh:Loader.LoadedMeshes)
    {
        for(int i=0;i<mesh.Vertices.size();i+=3)
        {
            Triangle* t = new Triangle();
            for(int j=0;j<3;j++)
            {
                t->setVertex(j,Vector4f(mesh.Vertices[i+j].Position.X,mesh.Vertices[i+j].Position.Y,mesh.Vertices[i+j].Position.Z,1.0));
                t->setNormal(j,Vector3f(mesh.Vertices[i+j].Normal.X,mesh.Vertices[i+j].Normal.Y,mesh.Vertices[i+j].Normal.Z));
                t->setTexCoord(j,Vector2f(mesh.Vertices[i+j].TextureCoordinate.X, mesh.Vertices[i+j].TextureCoordinate.Y));
            }
            TriangleList.push_back(t);
        }
    }

    rst::rasterizer r(700, 700);

    auto texture_path = "hmap.jpg";
    r.set_texture(Texture(obj_path + texture_path));

    std::function<Eigen::Vector3f(fragment_shader_payload)> active_shader = bump_fragment_shader;

    if (argc >= 2)
    {
        command_line = true;
        filename = std::string(argv[1]);

        if (argc == 3 && std::string(argv[2]) == "texture")
        {
            std::cout << "Rasterizing using the texture shader\n";
            active_shader = texture_fragment_shader;
            texture_path = "spot_texture.png";
            r.set_texture(Texture(obj_path + texture_path));
        }
        else if (argc == 3 && std::string(argv[2]) == "normal")
        {
            std::cout << "Rasterizing using the normal shader\n";
            active_shader = normal_fragment_shader;
        }
        else if (argc == 3 && std::string(argv[2]) == "phong")
        {
            std::cout << "Rasterizing using the phong shader\n";
            active_shader = phong_fragment_shader;
        }
        else if (argc == 3 && std::string(argv[2]) == "bump")
        {
            std::cout << "Rasterizing using the bump shader\n";
            active_shader = bump_fragment_shader;
        }
        else if (argc == 3 && std::string(argv[2]) == "displacement")
        {
            std::cout << "Rasterizing using the displacement shader\n";
            active_shader = displacement_fragment_shader;
        }
    }

    Eigen::Vector3f eye_pos = {0,0,10};

    r.set_vertex_shader(vertex_shader);
    r.set_fragment_shader(active_shader);

    int key = 0;
    int frame_count = 0;

    if (command_line)
    {
        r.clear(rst::Buffers::Color | rst::Buffers::Depth);
        r.set_model(get_model_matrix(angle));
        r.set_view(get_view_matrix(eye_pos));
        r.set_projection(get_projection_matrix(45.0, 1, 0.1, 50));

        r.draw(TriangleList);
        cv::Mat image(700, 700, CV_32FC3, r.frame_buffer().data());
        image.convertTo(image, CV_8UC3, 1.0f);
        cv::cvtColor(image, image, cv::COLOR_BGR2RGB);

        cv::imwrite(filename, image);

        return 0;
    }

    while(key != 27)
    {
        r.clear(rst::Buffers::Color | rst::Buffers::Depth);

        r.set_model(get_model_matrix(angle));
        r.set_view(get_view_matrix(eye_pos));
        r.set_projection(get_projection_matrix(45.0, 1, 0.1, 50));

        //r.draw(pos_id, ind_id, col_id, rst::Primitive::Triangle);
        r.draw(TriangleList);
        cv::Mat image(700, 700, CV_32FC3, r.frame_buffer().data());
        image.convertTo(image, CV_8UC3, 1.0f);
        cv::cvtColor(image, image, cv::COLOR_BGR2RGB);

        cv::imshow("image", image);
        cv::imwrite(filename, image);
        key = cv::waitKey(10);

        if (key == 'A' )
        {
            angle -= 0.1;
        }
        else if (key == 'D')
        {
            angle += 0.1;
        }

    }
    return 0;
}
