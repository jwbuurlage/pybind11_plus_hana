#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;

#include <boost/hana.hpp>
namespace hana = boost::hana;
using namespace hana::literals;
using namespace std::string_literals;

// ... old style packets
struct SomePacketOld {
    int32_t id;
    float some_payload;
};

struct AnotherPacketOld {
    int32_t id;
    std::vector<float> another_payload;
};

template <int D, typename T>
class TensorOld {};

// ... packets with introspection
struct SomePacket {
    BOOST_HANA_DEFINE_STRUCT(SomePacket, (int32_t, id), (float, some_payload));
};

struct AnotherPacket {
    BOOST_HANA_DEFINE_STRUCT(AnotherPacket, (int32_t, id),
                             (std::vector<float>, another_payload));
};

template <int D, typename T>
class Tensor {};

PYBIND11_MODULE(hanapy, m) {
    m.doc() = "Boost.Hana + pybind11 example";

    // old style user-defined bindings
    py::class_<SomePacketOld>(m, "some_packet_old")
        .def(py::init<int32_t, float>())
        .def_readwrite("id", &SomePacketOld::id)
        .def_readwrite("some_payload", &SomePacketOld::some_payload);

    py::class_<AnotherPacketOld>(m, "another_packet_old")
        .def(py::init<int32_t, std::vector<float>>())
        .def_readwrite("id", &AnotherPacketOld::id)
        .def_readwrite("another_payload", &AnotherPacketOld::another_payload);

    // old style tensor bindings
    py::class_<TensorOld<1, float>>(m, "tensor_old_1d_f");
    py::class_<TensorOld<2, float>>(m, "tensor_old_2d_f");
    py::class_<TensorOld<3, float>>(m, "tensor_old_3d_f");
    py::class_<TensorOld<4, float>>(m, "tensor_old_4d_f");
    py::class_<TensorOld<5, float>>(m, "tensor_old_5d_f");
    py::class_<TensorOld<6, float>>(m, "tensor_old_6d_f");
    py::class_<TensorOld<1, double>>(m, "tensor_old_1d_d");
    py::class_<TensorOld<2, double>>(m, "tensor_old_2d_d");
    py::class_<TensorOld<3, double>>(m, "tensor_old_3d_d");
    py::class_<TensorOld<4, double>>(m, "tensor_old_4d_d");
    py::class_<TensorOld<5, double>>(m, "tensor_old_5d_d");
    py::class_<TensorOld<6, double>>(m, "tensor_old_6d_d");

    // Hana style user-defined bindings
    auto packets = hana::make_tuple(
        hana::make_tuple("some_packet"s, hana::type_c<SomePacket>),
        hana::make_tuple("another_packet"s, hana::type_c<AnotherPacket>));

    hana::for_each(packets, [&](auto x) {
        // 1) get C++ type (e.g. SomePacket)
        using P = typename decltype(+(x[1_c]))::type;

        // 2) get arguments for the constructor, as a tuple of types
        auto types = hana::transform(hana::members(P{}), [](auto member) {
            return hana::type_c<decltype(member)>;
        });
        // ... types is now e.g. (int32_t, float) for SomePacket

        // 3) we 'unpack' this tuple inside py::init,
        using Init = typename decltype(hana::unpack(
            types, hana::template_<py::detail::initimpl::constructor>))::type;
        // ... now Init is e.g py::init<int32_t, float>

        // 4) register class with Python
        auto packet = py::class_<P>(m, x[0_c].c_str()).def(Init());

        // 5) register accessors
        hana::fold(hana::accessors<P>(), std::ref(packet),
                   [](py::class_<P>& c, auto ka) -> py::class_<P>& {
                       return c.def(hana::first(ka).c_str(), [&ka](P& p) {
                           return hana::second(ka)(p);
                       });
                   });
        // ... this may look complicated if you have not seen folds before, but
        // this is essentially iteratively calling .def on the registered packet
        // class. Note that we require the lambda indirection for pybind11 to
        // recognize the member function.
    });

    // Hana style template bindings
    auto ds = hana::make_tuple(
        hana::make_tuple("1d"s, 1_c), hana::make_tuple("2d"s, 2_c),
        hana::make_tuple("3d"s, 3_c), hana::make_tuple("4d"s, 4_c),
        hana::make_tuple("5d"s, 5_c), hana::make_tuple("6d"s, 6_c));

    auto ts = hana::make_tuple(hana::make_tuple("f"s, hana::type_c<float>),
                               hana::make_tuple("d"s, hana::type_c<double>));

    // ... for each (D, T) combination, we register the tensor class
    hana::for_each(hana::cartesian_product(hana::make_tuple(ds, ts)),
                   [&](auto dt) {
                       const auto d = dt[0_c][1_c];
                       using T = typename decltype(+dt[1_c][1_c])::type;
                       auto name = "tensor_"s + dt[0_c][0_c].c_str() + "_"s +
                                   dt[1_c][0_c].c_str();
                       py::class_<Tensor<d, T>>(m, name.c_str());
                   });
}
