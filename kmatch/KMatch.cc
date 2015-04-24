#include "kmatch/KMatch.h"

inline std::pair<int64_t,bool> str_to_kmer(const char * _str,uint8_t _K){
  int64_t key=0,rkey=0;
  for (uint8_t i=0;i<_K;i++){
    key<<=2;
    switch(_str[i]){
      case 'A':
      case 'a':
        key+=KMATCH_NUC_A;
        rkey+=((int64_t) KMATCH_NUC_T)<<(2*i);
        break;
      case 'C':
      case 'c':
        key+=KMATCH_NUC_C;
        rkey+=((int64_t) KMATCH_NUC_G)<<(2*i);
        break;
      case 'G':
      case 'g':
        key+=KMATCH_NUC_G;
        rkey+=((int64_t) KMATCH_NUC_C)<<(2*i);
        break;
      case 'T':
      case 't':
        key+=KMATCH_NUC_T;
        rkey+=((int64_t) KMATCH_NUC_A)<<(2*i);
        break;
      default:
        //XXX: not fancy at all!!!
        return std::pair<int64_t,bool>(KMATCH_NOKMER,0);
        break;
    }
  }
  return std::pair<int64_t,bool>((key < rkey ? key : rkey),(key<rkey));

};

KMatch::KMatch(char * _target_filename, char * _query_filename, uint8_t _K){
  target_filename=_target_filename;
  query_filename=_query_filename;
  K=_K;
};

void KMatch::kmer_array_from_fasta(char * filename, std::vector<kmer_position_t> & kposv, std::vector<seq_attributes_t> & seqnames){
  std::vector<kmer_position_t> karray;
  //read fasta and push_back(kmer,pos) (use pos as chr*CHR_CONST+offset)
  //open file
  std::string line,seq;
  std::ifstream fasta(filename);
  seq_attributes_t seq_attr;
  
  uint64_t max_freq=1;//XXX: make this an argument!!!
  std::pair<uint64_t,bool> ckmer;
  int64_t chr_offset=0;
  uint32_t seq_index=0;
  uint64_t kmer_index=0;
  //while (!EOF)
  std::cout<<"Loading fasta "<<filename<<" into kmer array"<<std::endl;
  while ( getline (fasta, line)){
    if ( (line.size()>0 && line[0]=='>') || fasta.eof()){
        if (seq.size()>0) {seq_attr.length=seq.size();
        kposv.resize(kposv.size()+seq.size()+1-K);
        seqnames.push_back(seq_attr);
        const char * s=seq.c_str();
        for (uint64_t p=0;p<seq.size()+1-K;p++){
          //TODO: this could well be parallel
          ckmer=str_to_kmer(s+p,K);
          kposv[kmer_index].kmer=ckmer.first;
          kposv[kmer_index].position=seq_index*KMATCH_POSITION_CHR_CNST+p+1;//1-based position
          if (ckmer.second) kposv[kmer_index].position=-kposv[kmer_index].position;
          kmer_index++;
        }
      }
      if (line.size()>0 && line[0]=='>'){
        //init new seq_attributes;
        std::cout<<"Loading sequence '"<<line<<"'"<<std::endl;
        seq_attr.name=line;
        seq="";
        seq_index++;
      }
    }
    else {
      seq+=line;
    }
  }
  fasta.close();
  std::cout<<"Kmer array with "<<kmer_index<<" elements created"<<std::endl;
  std::sort(kposv.begin(),kposv.end());
  std::cout<<"Kmer array sorted"<<std::endl;
  //TODO: allow only K elements, replace with KMATCH_NOKMER if an element is too high frequency?
  uint64_t ri,wi=0,f;
  uint64_t kposv_size=kposv.size();
  for (ri=0;ri<kposv_size;ri++){
    for (f=0;kposv[ri+f].kmer==kposv[ri].kmer;f++);
    if (f>max_freq) {
      ri+=f-1;//jump to the last one
      continue; //jump to the next cycle
    }
    kposv[wi++]=kposv[ri];
  }
  kposv.resize(wi);
  std::cout<<"Kmer array filtered to "<<wi<<"elements"<<std::endl;
}

void KMatch::load_positions(){
  kmer_array_from_fasta(target_filename,target_positions,target_seqs);
  kmer_array_from_fasta(query_filename,query_positions,query_seqs);
}

void KMatch::merge_positions(){
  //TODO: parallel, each thread can take 1/t of the first array and scan the second array until finding the suitable start.
  std::cout<<"Starting to create matching positions"<<std::endl;
  uint64_t ti=0;
  kmer_match_t m;
  uint64_t tsize=target_positions.size();
  uint64_t qsize=query_positions.size();
  for (uint64_t qi=0;qi<qsize;qi++){
    //advance target
    while (ti<tsize && query_positions[qi].kmer>target_positions[ti].kmer) ti++;
    //match on target?
    if (query_positions[qi].kmer==target_positions[ti].kmer){
      //check for multi-match
      for (uint64_t j=0; ti+j<tsize && query_positions[qi].kmer==target_positions[ti+j].kmer; j++) {
        bool rev=false;
        //insert each result XXX insert the real position + 1 to allow for sign
        if (query_positions[qi].position>0){
          m.q_position=query_positions[qi].position;
        }else{
          rev=true;
          m.q_position=-query_positions[qi].position;
        }
        if (target_positions[ti+j].position>0){
          m.t_position=target_positions[ti+j].position;
        } else {
          rev=!rev;
          m.t_position=-target_positions[ti+j].position;
        }
        m.reverse=rev;
        kmatches.push_back(m);
      }
    }
  }
  std::cout<<kmatches.size()<<" matching positions"<<std::endl;

}

void KMatch::clear_positions(){
  target_positions.clear();
  query_positions.clear();
}
void KMatch::dump_matching_blocks(char * out_filename, int min_length, int max_jump){
  //XXX: allow for multi matches!!
  //watch out: matching positions are 1-based to allow for sign always
  uint64_t kmsize=kmatches.size();
  std::cout<<"Sorting the matches"<<std::endl;
  std::sort(kmatches.begin(),kmatches.end());//XXX: this needs to be sorted by absolute value!
  std::cout<<"Dumping matches of "<<min_length-K << " kmers with jumps of up to "<<max_jump<<" to "<<out_filename<<std::endl;
  std::ofstream out_file(out_filename);
  int64_t match_start=0;
  int64_t q_delta, t_delta;
  uint64_t dumped=0;
  for (uint64_t i=1;i<kmsize ;i++){//do not check the last element, check it outside!
    //if match breaks in this element
    q_delta=kmatches[i].q_position-kmatches[i-1].q_position;
    t_delta=kmatches[i].t_position-kmatches[i-1].t_position;
    if ( kmatches[i].reverse != kmatches[i-1].reverse //change in direction
         || q_delta-1>max_jump //long jump
         || (kmatches[i].reverse==false && q_delta != t_delta)//direct and not same difference
         || (kmatches[i].reverse==true && q_delta != -t_delta) ) { //reverse and not same difference 

      //length>min_length?
      //std::cout<<"evaluating match in ["<<match_start<<"-"<<i-1<<"] "<<q_delta<<" "<<t_delta<<" "<<kmatches[i].reverse<<" "<<kmatches[i-1].reverse<<"||"<<(kmatches[i].reverse != kmatches[i-1].reverse)<<" "<<(q_delta-1>max_jump)<<" "<< (kmatches[i].reverse==false && q_delta != t_delta)<<" "<<(kmatches[i].reverse==true && q_delta != -t_delta)<<std::endl;
      if (i-match_start>=min_length-K){
        //TODO:dump
        t_result r;
        r.query_id=kmatches[match_start].q_position/KMATCH_POSITION_CHR_CNST;
        r.target_id=kmatches[match_start].t_position/KMATCH_POSITION_CHR_CNST;
        r.query_size=query_seqs[r.query_id].length;
        r.qstart=kmatches[match_start].q_position%KMATCH_POSITION_CHR_CNST-1;//0-based
        r.tstart=kmatches[match_start].t_position%KMATCH_POSITION_CHR_CNST-1;//0-based
        r.len=kmatches[i-1].q_position - kmatches[match_start].q_position;
        r.reverse=kmatches[match_start].reverse;
        r.prob=1;
        r.ident=1;
        out_file.write((char *) &r,sizeof(r));
        dumped++;
      }
      //move match start
      match_start=i;
    }
  }
  out_file.close();
  std::cout<<dumped<<" matches dumped"<<std::endl;
}

int main(int argc, char ** argv){
  KMatch kmatch(argv[1],argv[2],atoi(argv[3]));
  kmatch.load_positions();
  kmatch.merge_positions();
  kmatch.clear_positions();
  kmatch.dump_matching_blocks(argv[4],atoi(argv[5]),atoi(argv[6]));
}
