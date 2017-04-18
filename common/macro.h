#ifndef MACRO_H
#define MACRO_H

#define SCOPED_PTR(CLASS, INSTANCE, NAME) do {\
     boost::scoped_ptr<CLASS> NAME(INSTANCE);\
  } while(0) 

#endif
