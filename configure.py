import os
import shutil
import subprocess

# 初始化 Git 子模块
subprocess.run(["git", "submodule", "update", "--init", "--recursive"])

# 设置工作目录
base_dir = os.path.join(os.getcwd(), "environment_assets")
cli_path = os.path.join(os.getcwd(), "tools", "cli.exe")

# 拷贝 lut_sheen_E.png 到 environment_assets 文件夹
lut_file = os.path.join(os.getcwd(), "tools", "lut_sheen_E.png")
destination_path = os.path.join(base_dir, "lut_sheen_E.png")
if not os.path.exists(destination_path):
    shutil.copy(lut_file, destination_path)
    print(f"Copied {lut_file} to {destination_path}")
    
# 获取 environment_assets 目录下所有 .hdr 文件
hdr_files = [f for f in os.listdir(base_dir) if f.endswith('.hdr')]

# 遍历每个 .hdr 文件
for hdr_file in hdr_files:
    # 获取文件名（不带扩展名）作为目录名
    cur_dir = os.path.splitext(hdr_file)[0]
    cur_dir_path = os.path.join(base_dir, cur_dir)
    print(f"Processing: {cur_dir}")

    # 如果目录不存在，创建目录
    if not os.path.exists(cur_dir_path):
        os.makedirs(cur_dir_path)

    # 创建子目录 lambertian, ggx, charlie
    for sub_dir in ["lambertian", "ggx", "charlie"]:
        full_path = os.path.join(cur_dir_path, sub_dir)
        if not os.path.exists(full_path):
            os.makedirs(full_path)

    # 构建命令行调用
    hdr_file_path = os.path.join(base_dir, hdr_file)
    output_path_lambertian = os.path.join(cur_dir_path, "lambertian", "diffuse.ktx2")
    output_path_ggx = os.path.join(cur_dir_path, "ggx", "specular.ktx2")
    output_path_charlie = os.path.join(cur_dir_path, "charlie", "sheen.ktx2")

    # Lambertian 命令
    subprocess.run([
        cli_path, 
        "-inputPath", hdr_file_path, 
        "-distribution", "Lambertian", 
        "-outCubeMap", output_path_lambertian, 
        "-cubeMapResolution", "256", 
        "-sampleCount", "2048", 
        "-lodBias", "0", 
        "-mipLevelCount", "5"
    ])

    # GGX 命令
    subprocess.run([
        cli_path, 
        "-inputPath", hdr_file_path, 
        "-distribution", "GGX", 
        "-outCubeMap", output_path_ggx, 
        "-cubeMapResolution", "256", 
        "-sampleCount", "1024", 
        "-lodBias", "0", 
        "-mipLevelCount", "5",
        "-outLUT", "environment_assets/lut_ggx.png"
    ])

    # Charlie 命令
    subprocess.run([
        cli_path, 
        "-inputPath", hdr_file_path, 
        "-distribution", "Charlie", 
        "-outCubeMap", output_path_charlie, 
        "-cubeMapResolution", "256", 
        "-sampleCount", "64", 
        "-lodBias", "0", 
        "-mipLevelCount", "5",
        "-outLUT", "environment_assets/lut_charlie.png"
    ])

print("All tasks completed.")
