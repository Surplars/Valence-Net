# GSIM compatibility

Pinned GSIM: 93b8cd23edd3228807c4f2a08c19c3936a463cb2.

During integration, a WireDefault(false.B) with several nested conditional true
assignments produced an uninitialized C++ local in the model's abort condition.
The equivalent RTL had a defined default. The application now expresses abort as
one explicit Boolean expression. No generated model or upstream simulator was
patched. Normal and error-path real-client tests cover the resulting logic.

Top-level Vec bundle ports also have the existing scalar-accessor limitation;
AppHarness uses explicitly named control/data test ports. Production application
ports remain Vec(2), and the complete GMII harness has scalar external ports.
