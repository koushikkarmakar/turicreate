#define BOOST_TEST_MODULE
#include <boost/test/unit_test.hpp>
#include <util/test_macros.hpp>
#include <stdlib.h>
#include <vector>
#include <string>
#include <functional>
#include <random>
#include <cfenv>

#include <ml_data/ml_data.hpp>
#include <optimization/optimization_interface.hpp>
#include <optimization/utils.hpp>
#include <toolkits/supervised_learning/supervised_learning.hpp>
#include <toolkits/supervised_learning/linear_svm.hpp>
#include <toolkits/supervised_learning/linear_svm_opt_interface.hpp>
#include <sframe/testing_utils.hpp>


using namespace turi;
using namespace turi::supervised;

void run_linear_svm_test(std::map<std::string, flexible_type> opts) {


  size_t examples = opts.at("examples");
  size_t features = opts.at("features");
  std::string target_column_name = "target";

  // Answers
  // -----------------------------------------------------------------------
  DenseVector coefs(features+1);
  coefs.randn();

  // Feature names
  std::vector<std::string> feature_names;
  std::vector<flex_type_enum> feature_types;
  for(size_t i=0; i < features; i++){
    feature_names.push_back(std::to_string(i));
    feature_types.push_back(flex_type_enum::FLOAT);
  }

  // Data
  std::vector<std::vector<flexible_type>> y_data;
  std::vector<std::vector<flexible_type>> X_data;
  for(size_t i=0; i < examples; i++){
    DenseVector x(features);
    x.randn();
    std::vector<flexible_type> x_tmp;
    for(size_t k=0; k < features; k++){
      x_tmp.push_back(x(k));
    }

    // Compute the prediction for this
    double t = dot(x, coefs.subvec(0, features-1)) + coefs(features);
    t = 1.0/(1.0+exp(-1.0*t));
    int c = turi::random::bernoulli(t);
    if (i == 0) c = 0; // Make sure category 0 is category 0 (for testing)
    std::vector<flexible_type> y_tmp;
    y_tmp.push_back(c);

    X_data.push_back(x_tmp);
    y_data.push_back(y_tmp);
  }

  // Options
  std::map<std::string, flexible_type> options = {
    {"convergence_threshold", 1e-2},
    {"max_iterations", 10},
    {"solver", "lbfgs"},
  };

  // Make the data
  sframe X = make_testing_sframe(feature_names, feature_types, X_data);
  sframe y = make_testing_sframe({"target"}, {flex_type_enum::STRING}, y_data);
  std::shared_ptr<linear_svm> model;
  model.reset(new linear_svm);
  model->init(X,y);
  model->init_options(options);
  model->train();

  // Construct the ml_data
  ml_data data = model->construct_ml_data_using_current_metadata(X, y);
  ml_data valid_data;

  // Check coefficients & options
  // ----------------------------------------------------------------------
  DenseVector _coefs(features+1);
  model->get_coefficients(_coefs);
  TS_ASSERT(_coefs.size() == features + 1);

  std::map<std::string, flexible_type> _options;
  _options = model->get_current_options();
  for (auto& kvp: options){
    TS_ASSERT(_options[kvp.first] == kvp.second);
  }
  TS_ASSERT(model->is_trained() == true);

  // Check predictions
  // ----------------------------------------------------------------------
  std::shared_ptr<sarray<flexible_type>> _pred_class;
  std::vector<flexible_type> pred_class;
  _pred_class = model->predict(data, "class");

  // Save predictions made by the model
  size_t rows;
  auto reader = _pred_class->get_reader();
  rows = reader->read_rows(0, examples, pred_class);

  // Check that the predictions made by the model are right!
  for(size_t i=0; i < examples; i++){
    DenseVector x(features + 1);
    for(size_t k=0; k < features; k++){
      x(k) = X_data[i][k];
    }
    x(features) = 1;
    double t = arma::dot(x, _coefs);
    int c = t > 0.0;
    TS_ASSERT_EQUALS(pred_class[i], std::to_string(c));
  }


  // Check save and load
  // ----------------------------------------------------------------------
  dir_archive archive_write;
  archive_write.open_directory_for_write("linear_svm_tests");
  turi::oarchive oarc(archive_write);
  oarc << *model;
  archive_write.close();

  // Load it
  dir_archive archive_read;
  archive_read.open_directory_for_read("linear_svm_tests");
  turi::iarchive iarc(archive_read);
  iarc >> *model;


  // Check predictions after saving and loading.
  // ----------------------------------------------------------------------
  DenseVector _coefs_after_load(features+1);
  model->get_coefficients(_coefs_after_load);
  TS_ASSERT(_coefs_after_load.size() == features + 1);
  TS_ASSERT(arma::approx_equal(_coefs_after_load, _coefs,"absdiff", 1e-5));
  _options = model->get_current_options();
  for (auto& kvp: options){
    TS_ASSERT(_options[kvp.first] == kvp.second);
  }
  TS_ASSERT(model->is_trained() == true);


  // Check coefficients after saving and loading.
  // ----------------------------------------------------------------------
  _pred_class = model->predict(data, "class");
  reader = _pred_class->get_reader();
  rows = reader->read_rows(0, examples, pred_class);

  // Check that the predictions made by the model are right!
  for(size_t i=0; i < examples; i++){
    DenseVector x(features + 1);
    for(size_t k=0; k < features; k++){
      x(k) = X_data[i][k];
    }
    x(features) = 1;
    double t = arma::dot(x, _coefs);
    int c = t > 0.0;
    TS_ASSERT_EQUALS(pred_class[i], std::to_string(c));
  }

  model->get_coefficients(_coefs);
  TS_ASSERT(_coefs.size() == features + 1);
  model.reset();
}

/**
 *  Check linear svm
*/
struct linear_svm_test  {

  public:

  void test_linear_svm_basic_2d() {
    std::map<std::string, flexible_type> opts = {
      {"examples", 100},
      {"features", 1}};
    run_linear_svm_test(opts);
  }

  void test_linear_svm_small() {
    std::map<std::string, flexible_type> opts = {
      {"examples", 1000},
      {"features", 10}};
    run_linear_svm_test(opts);
  }

};



void run_linear_svm_scaled_logistic_opt_interface_test(std::map<std::string,
    flexible_type> opts) {


  size_t examples = opts.at("examples");
  size_t features = opts.at("features");
  std::string target_column_name = "target";
  std::vector<std::string> column_names = {"user", "item"};

  // Answers
  // -----------------------------------------------------------------------
  DenseVector coefs(features+1);
  coefs.randn();

  // Feature names
  std::vector<std::string> feature_names;
  std::vector<flex_type_enum> feature_types;
  for(size_t i=0; i < features; i++){
    feature_names.push_back(std::to_string(i));
    feature_types.push_back(flex_type_enum::FLOAT);
  }

  // Data
  std::vector<std::vector<flexible_type>> y_data;
  std::vector<std::vector<flexible_type>> X_data;
  for(size_t i=0; i < examples; i++){
    DenseVector x(features);
    x.randn();
    std::vector<flexible_type> x_tmp;
    for(size_t k=0; k < features; k++){
      x_tmp.push_back(x(k));
    }

    // Compute the prediction for this
    double t = dot(x, coefs.subvec(0, features-1)) + coefs(features);
    t = 1.0/(1.0+exp(-1.0*t));
    int c = turi::random::bernoulli(t);
    std::vector<flexible_type> y_tmp;
    y_tmp.push_back(c);

    X_data.push_back(x_tmp);
    y_data.push_back(y_tmp);
  }


  // Construct the ml_data
  // Make the data
  sframe X = make_testing_sframe(feature_names, feature_types, X_data);
  sframe y = make_testing_sframe({"target"}, {flex_type_enum::STRING}, y_data);
  std::shared_ptr<linear_svm> model;
  model.reset(new linear_svm);
  model->init(X,y);

  // Construct the ml_data
  ml_data data = model->construct_ml_data_using_current_metadata(X, y);
  ml_data valid_data;

  std::shared_ptr<linear_svm_scaled_logistic_opt_interface> svm_interface;
  svm_interface.reset(new linear_svm_scaled_logistic_opt_interface(data, valid_data, *model));

  // Check examples & variables.
  TS_ASSERT(svm_interface->num_variables() == features+1);
  TS_ASSERT(svm_interface->num_examples() == examples);

  size_t variables = svm_interface->num_variables();
  for(size_t i=0; i < 10; i++){

    DenseVector point(variables);
    point.randn();

    // Check gradients, functions and hessians.
    DenseVector gradient(variables);
    double func_value;

    func_value = svm_interface->compute_function_value(point);
    svm_interface->compute_gradient(point, gradient);
  }

  model.reset();
  svm_interface.reset();
}


/**
 *  Check opt interface
*/
struct linear_svm_scaled_logistic_opt_interface_test  {

  public:

  void test_linear_svm_scaled_logistic_opt_interface_basic_2d() {
    std::map<std::string, flexible_type> opts = {
      {"examples", 100},
      {"features", 1}};
    run_linear_svm_scaled_logistic_opt_interface_test(opts);
  }

  void test_linear_svm_scaled_logistic_opt_interface_small() {
    std::map<std::string, flexible_type> opts = {
      {"examples", 1000},
      {"features", 10}};
    run_linear_svm_scaled_logistic_opt_interface_test(opts);
  }

};

BOOST_FIXTURE_TEST_SUITE(_linear_svm_test, linear_svm_test)
BOOST_AUTO_TEST_CASE(test_linear_svm_basic_2d) {
  linear_svm_test::test_linear_svm_basic_2d();
}
BOOST_AUTO_TEST_CASE(test_linear_svm_small) {
  linear_svm_test::test_linear_svm_small();
}
BOOST_AUTO_TEST_SUITE_END()
BOOST_FIXTURE_TEST_SUITE(_linear_svm_scaled_logistic_opt_interface_test, linear_svm_scaled_logistic_opt_interface_test)
BOOST_AUTO_TEST_CASE(test_linear_svm_scaled_logistic_opt_interface_basic_2d) {
  linear_svm_scaled_logistic_opt_interface_test::test_linear_svm_scaled_logistic_opt_interface_basic_2d();
}
BOOST_AUTO_TEST_CASE(test_linear_svm_scaled_logistic_opt_interface_small) {
  linear_svm_scaled_logistic_opt_interface_test::test_linear_svm_scaled_logistic_opt_interface_small();
}
BOOST_AUTO_TEST_SUITE_END()