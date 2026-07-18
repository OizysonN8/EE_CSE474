#include <string>

/**
 * @brief Class for testing Doxygen
 * @details Contains two fields, a constructor, and a function
 * 
 */
class DoxygenTest {
    private:
        double test_number;
        std::string test_name;

    public:
        /**
         * @brief Construct a new Doxygen Test object
         * 
         * @param test_name A test name
         * @param test_number A test number
         */
        DoxygenTest(std::string test_name, double test_number);

        /**
         * @brief 
         * 
         * @param another_number Another number
         * @return void This does nothing
         */
        void aFunction(double another_number);
}
