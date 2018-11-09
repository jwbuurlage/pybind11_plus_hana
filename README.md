# Using Boost.Hana to simplify the generation of Python bindings<a id="sec-1"></a>

Recently, I have implemented Python bindings for a number of software libraries I work on. For this, I have used the excellent [pybind11](https://github.com/pybind/pybind11), which makes it easy to provide bindings for basic C++ functions and classes. Generating bindings for functions and classes looks like this:

```cpp
// bindings for a function
m.def("add", &add, "A function which adds two numbers");
// bindings for a class
py::class_<Pet>(m, "Pet").def(py::init<const std::string &>())
```

While writing these bindings, I ran into two problems for two different projects, and in both cases they lead to lengthy binding definitions with a lot of repeated code.

## Problem 1: *Many aggregate data types*<a id="sec-1-1"></a>

One project had a lot of aggregate data types, and generating bindings for them required repeating their fields three times: in the definition, when exposing the constructor to Python, and for defining the accessors to the fields. This will inevitably lead to frustrating maintenance work as fields get added and removed. In my case, these data types described packets that would be sent over a network connection, and they would look something like this:

```cpp
struct SomePacket {
  static constexpr auto desc = descriptor::some_packet;
  int32_t id;
  float some_payload;
  // ... more fields
};

struct AnotherPacket {
  static constexpr auto desc = descriptor::another_packet;
  int32_t id;
  std::vector<float> another_payload;
  // ... more fields
};

// ... many more
```

Of course in reality packets typically have more than two fields. The binding code would look like this:

```cpp
  py::class_<SomePacket>(m, "some_packet")
    .def(py::init<int32_t, float>())
    .def_readwrite("id", &SomePacket::id)
    .def_readwrite("some_payload", &SomePacket::some_payload)
    .def_readwrite(...);

  py::class_<AnotherPacket>(m, "another_packet")
    .def(py::init<int32_t, std::vector<float>>())
    .def_readwrite("id", &AnotherPacket::id)
    .def_readwrite("another_payload", &AnotherPacket::another_payload)
    .def_readwrite(...);

// ... many more
```

## Problem 2: *Templates*<a id="sec-1-2"></a>

A second problem is when you want to generate bindings to templates. In another project, I had many classes and functions that were generic over two parameters: one integer that defines the dimension of the object or problem, and one type parameter representing the scalar type that is used (typically `float` or `double`, but maybe in some cases half precision or even quadruple precision floats). An example class template is the following.

```cpp
template <int D, typename T>
class Tensor {};
```

Generating bindings for these objects requires explicitly instantiating them for all the values of `D` and `T` that you want to support. This will quickly become cumbersome. Already for this single class, for a limited number of values for `D` and `T`, the binding code becomes a mess:

```cpp
py::class_<Tensor<1, float>>(m, "tensor_1d_f");
py::class_<Tensor<2, float>>(m, "tensor_2d_f");
py::class_<Tensor<3, float>>(m, "tensor_3d_f");
py::class_<Tensor<4, float>>(m, "tensor_4d_f");
py::class_<Tensor<5, float>>(m, "tensor_5d_f");
py::class_<Tensor<6, float>>(m, "tensor_6d_f");
py::class_<Tensor<1, double>>(m, "tensor_1d_d");
py::class_<Tensor<2, double>>(m, "tensor_2d_d");
py::class_<Tensor<3, double>>(m, "tensor_3d_d");
py::class_<Tensor<4, double>>(m, "tensor_4d_d");
py::class_<Tensor<5, double>>(m, "tensor_5d_d");
py::class_<Tensor<6, double>>(m, "tensor_6d_d");
```

## Metaprogramming to the rescue!<a id="sec-1-3"></a>

With *Boost.Hana*, it is possible to write concise and maintainable Python bindings for both of these problematic cases. For the uninitiated:

> Hana is a header-only library for C++ metaprogramming suited for computations on both types and values.

&#x2026; and computations on types seems to be exactly what we need.

### Automatically binding user-defined types<a id="sec-1-3-1"></a>

First, let us focus on generating bindings to simple user-defined types. In order to avoid listing the (types of the) fields when generating the bindings, we need to be able to *inspect* our data types programmatically. This *introspection* of user-defined types is supported by Boost.Hana using either of two macro's.

`BOOST_HANA_DEFINE_STRUCT` can be used within the original definition of a struct, or `BOOST_HANA_ADAPT_STRUCT` can be used outside of the original definition (if you cannot, or understandably do not want to touch the definition of your data types). In our example, this would look like this:

```cpp
struct SomePacket {
  static constexpr auto desc = descriptor::some_packet;
  BOOST_HANA_DEFINE_STRUCT(SomePacket,
    (int32_t, id),
    (float, some_payload));
}

struct AnotherPacket {
  static constexpr auto desc = descriptor::another_packet;
  BOOST_HANA_DEFINE_STRUCT(AnotherPacket,
    (int32_t, id),
    (std::vector<float>, another_payload));
}
```

Now, we have the possibility to loop over the members fields of our packets. This can also simplify code in other places. For example, these network packets have to be serialized, deserialized and measured for size. This can now all be implemented in a function with a one-line body!

```cpp
template <typename Packet, typename Buffer>
void fill(Packet& packet, Buffer& buffer) {
    hana::for_each(packet, [&](auto member) { buffer | hana::second(member); });
}
```

Here, `Buffer` is a class (`serializer`, `deserializer`, or a `scale`) that implements `operator|` for all the possible field types. However, these individual functions do not have to be implemented for each packet. With the `for_each` function, we are able to loop over all `(name, value)` pairs for the member fields of our packets.

Going back to the Python bindings, being able to loop over all member fields means we no longer have to explicitly list constructors and accessors. We can generate them automatically!

```cpp
// 1) list packets and the names to give to their Python bindings
auto packets = hana::make_tuple(
    hana::make_tuple("some_packet"s, hana::type_c<SomePacket>),
    hana::make_tuple("another_packet"s, hana::type_c<AnotherPacket>),
  // ... many more
);

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
        auto packet = py::class_<P, Packet>(m, x[0_c].c_str()).def(Init());
        // ... x[0_c] contains the python name, e.g. some_packet, and Packet is
        // some base class.

        // 5) register accessors
        hana::fold(hana::accessors<P>(), std::ref(packet),
                   [](py::class_<P, Packet>& c,
                      auto ka) -> py::class_<P, Packet>& {
                       return c.def(hana::first(ka).c_str(), [&ka](P& p) {
                           return hana::second(ka)(p);
                       });
                   });
        // ... this may look complicated if you have not seen folds before, but
        // this is essentially iteratively calling .def on the registered packet
        // class. Note that we require the lambda indirection for pybind11 to
        // recognize the member function.
    });
```

Now, generating bindings for a new packet is completely automatic: we only have to add it to the `packets` list. Also, when changing/adding/removing fields from a packet, the Python bindings are updated automatically. Neat!

### Automatically instantiating templates<a id="sec-1-3-2"></a>

We can also use Boost.Hana to generate combinations of `D` and `T` in our `Tensor` example.

```cpp
auto ds = hana::make_tuple(
    hana::make_tuple("1d"s, 1_c), hana::make_tuple("2d"s, 2_c),
    hana::make_tuple("3d"s, 3_c), hana::make_tuple("4d"s, 4_c),
    hana::make_tuple("5d"s, 5_c), hana::make_tuple("6d"s, 6_c));

auto ts = hana::make_tuple(hana::make_tuple("f"s, hana::type_c<float>),
                           hana::make_tuple("d"s, hana::type_c<double>));

hana::for_each(hana::cartesian_product(hana::make_tuple(ds, ts)),
    [&](auto dt) {
      const auto d = dt[0_c][1_c];
      using T = typename decltype(+dt[1_c][1_c])::type;
      auto name = "tensor_"s + dt[0_c][0_c].c_str() + "_"s +
                  dt[1_c][0_c].c_str();
      py::class_<Tensor<d, T>>(m, name.c_str());
    });
```
