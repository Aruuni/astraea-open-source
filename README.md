# Astraea

Astraea is a performant DRL-based congestion control algorithm for the Internet. It optimizes convergence properties, including fairness, responsiveness, and stability, while maintaining high throughput, low latency, and low packet loss rates.

Astraea's prototype is built on top of [Puffer](https://github.com/StanfordSNR/puffer/tree/master)'s network library.

## Build Astraea

### Dependencies

- [mahimahi](https://github.com/ravinet/mahimahi.git) for the emulated network environment
- [json](https://github.com/nlohmann/json) for message serialization and deserialization
- [Astraea kernel](kernel/deb/README.md)
- [Astraea customized kernel TCP CC module](kernel/tcp-astraea/README.md)
- [TensorflowCC](https://github.com/FloopCZ/tensorflow_cc) for Astraea's inference service
- Boost
- g++: We recommend using g++-9
- TensorFlow: 1.14.0

### Clone Astraea

```bash
git clone https://github.com/HKUST-SING/astraea --recursive
```

### Setup Customized Kernel

Astraea relies on a customized kernel that supports gathering TCP statistics from the kernel TCP datapath. Please refer to the [kernel](kernel/deb/README.md) for more details.

### Build Astraea Kernel TCP CC Module

Astraea also uses a customized kernel TCP CC module. Please refer to the [kernel cc module](kernel/tcp-astraea/README.md) for more details.

### Build Astraea Client and Server

```bash
cd src
mkdir build && cd build
# If you want to use Astraea's inference service, add -DCOMPILE_INFERENCE_SERVICE=ON
CXX=/usr/bin/g++-9 cmake ..
make -j
```
### Install python dependencies 

```bash 
python3.7 -m pip install pip --upgrade
python3.7 -m pip install protobuf==3.10.0 tensorflow==1.14.0 --upgrade
python3.7 -m pip install matplotlib==3.2
```
python3.7 -m pip install gym sysv_ipc matplotlib numpy pyyaml tabulate

### Build Astraea Inference Service (Optional)

> **Note:** Astraea's batch inference services rely on TensorflowCC, a C++ API for TensorFlow. The current version of Astraea's inference server is built on TensorFlow 1.15.0. To compile TensorFlow, you need to install Bazel and other dependencies, such as CUDA and cuDNN, if you want to use GPU. Please refer to the official TensorFlow website for more details.

```bash
cd scripts/
./install_tensorflow_cc.sh
```

## Run Astraea

### Run Astraea Server

```bash
./src/build/bin/server --port=12345 --perf-log=test/server.txt --perf-interval=1000 --one-off
```

### Run Astraea Client with Naive Python Inference Helper

> **Note:** Ensure that you have allowed `astraea` as the kernel TCP congestion control algorithm.

```bash
./src/build/bin/client_eval --ip=127.0.0.1 --port=12345 --cong=astraea --interval=30 --pyhelper=./python/infer.py --model=./models/py/ --duration=10 --id=0 --perf-log=test/client.txt
```

### Run Astraea with Mahimahi

To run Astraea with mahimahi, use the following commands:

1. Launch the server as mentioned above.
2. Launch the client with the following command inside the mahimahi shell:

```bash
mm-delay 10
# make sure IP forwarding is allowed: sudo sysctl -w net.ipv4.ip_forward=1
# Inside the mahimahi shell
./src/build/bin/client_eval --ip=$MAHIMAHI_BASE \
    --port=12345 \
    --cong=astraea \
    --interval=30 \
    --pyhelper=./python/infer.py \
    --model=./models/py/
```

### Run Astraea Inference Service (Optional)

#### Run Astraea Inference Service Using UDP Channel

1. To run Astraea inference service with a pre-trained model using a UDP channel in the background, use the following command:

```bash
./src/build/bin/infer --graph ./models/exported/model.meta --checkpoint ./models/exported/model --batch=0 --channel=udp
```

2. Start the server:

```bash
./src/build/bin/server --port=12345
```

3. Start the client (client and inference service communicate via UDP port 8888):

```bash
./src/build/bin/client_eval_batch_udp --ip=127.0.0.1 --port=12345 --cong=astraea --interval=30
```

#### Run Astraea with Mahimahi Using Unix Socket Channel

> Note that, the UDP channel of Astraea's inference service might not work in mahimahi, as it introduces additional network namespaces.

To run Astraea with mahimahi using the inference service with a Unix socket channel, use the following commands (Astraea client and inference service communicate via `/tmp/astraea.sock`):

1. Run the inference service:

```bash
# set --batch=1 if you want to use batch inference mode
./src/build/bin/infer --graph ./models/exported/model.meta --checkpoint ./models/exported/model --batch=0 --channel=unix
```

2. Run the client:

```bash
mm-delay 10
# Inside the mahimahi shell
./src/build/bin/client_eval_batch --ip=$MAHIMAHI_BASE \
    --port=12345 \
    --cong=astraea \
    --interval=30
```

## Reference

The design, implementation, and evaluation of Astraea are detailed in the following paper presented at EuroSys '24:

```bib
@inproceedings{Astraea,
    author = {Liao, Xudong and Tian, Han and Zeng, Chaoliang and Wan, Xinchen and Chen, Kai},
    title = {Astraea: Towards Fair and Efficient Learning-based Congestion Control},
    year = {2024},
    url = {https://doi.org/10.1145/3627703.3650069},
    booktitle = {Proceedings of the Nineteenth European Conference on Computer Systems},
    series = {EuroSys '24}
}
```

More details about Astraea's multi-flow environment are coming soon.
